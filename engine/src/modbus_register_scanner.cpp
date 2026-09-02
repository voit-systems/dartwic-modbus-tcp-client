#include "modbus_register_scanner.h"

#include <modbus/modbus.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int max_modbus_address = 0xFFFF;
constexpr int max_scan_span = 4096;

struct ContextDeleter {
    void operator()(modbus_t* context) const {
        if (context == nullptr) return;
        modbus_close(context);
        modbus_free(context);
    }
};

using Context = std::unique_ptr<modbus_t, ContextDeleter>;

struct TableScan {
    bool supported{true};
    std::vector<int> addresses;
};

enum class TableType { Coils, DiscreteInputs, HoldingRegisters, InputRegisters };

const char* tableLabel(TableType type) {
    switch (type) {
        case TableType::Coils: return "Coil";
        case TableType::DiscreteInputs: return "Discrete input";
        case TableType::HoldingRegisters: return "Holding register";
        case TableType::InputRegisters: return "Input register";
    }
    return "Modbus";
}

int integerValue(const nlohmann::json& request, const char* key, int fallback) {
    if (!request.contains(key)) return fallback;
    if (!request[key].is_number_integer()) {
        throw std::runtime_error(std::string(key) + " must be an integer.");
    }
    return request[key].get<int>();
}

TableScan scanTable(modbus_t* context, int start_address, int end_address, TableType type,
    std::size_t& request_count) {
    TableScan result;
    uint16_t register_value = 0;
    uint8_t bit_value = 0;
    for (int address = start_address; address <= end_address; ++address) {
        errno = 0;
        int read_count = -1;
        switch (type) {
            case TableType::Coils:
                read_count = modbus_read_bits(context, address, 1, &bit_value);
                break;
            case TableType::DiscreteInputs:
                read_count = modbus_read_input_bits(context, address, 1, &bit_value);
                break;
            case TableType::HoldingRegisters:
                read_count = modbus_read_registers(context, address, 1, &register_value);
                break;
            case TableType::InputRegisters:
                read_count = modbus_read_input_registers(context, address, 1, &register_value);
                break;
        }
        ++request_count;
        if (read_count == 1) {
            result.addresses.push_back(address);
            continue;
        }
        if (errno == EMBXILADD) continue;
        if (errno == EMBXILFUN) {
            result.supported = false;
            result.addresses.clear();
            break;
        }
        throw std::runtime_error(
            std::string(tableLabel(type)) + " scan failed at address " +
            std::to_string(address) + ": " + modbus_strerror(errno));
    }
    return result;
}

nlohmann::json compactRanges(const std::vector<int>& addresses) {
    auto ranges = nlohmann::json::array();
    if (addresses.empty()) return ranges;
    int start = addresses.front();
    int end = start;
    for (std::size_t index = 1; index < addresses.size(); ++index) {
        if (addresses[index] == end + 1) {
            end = addresses[index];
            continue;
        }
        ranges.push_back({{"start", start}, {"end", end}, {"count", end - start + 1}});
        start = end = addresses[index];
    }
    ranges.push_back({{"start", start}, {"end", end}, {"count", end - start + 1}});
    return ranges;
}

nlohmann::json tablePayload(const TableScan& scan) {
    return {
        {"supported", scan.supported},
        {"available_count", scan.addresses.size()},
        {"ranges", compactRanges(scan.addresses)},
    };
}
} // namespace

nlohmann::json scanModbusRegisters(const nlohmann::json& request) {
    if (!request.is_object()) throw std::runtime_error("Register scan request must be an object.");
    const std::string host = request.value("server_ip", std::string{});
    const int port = integerValue(request, "server_port", 502);
    const int unit_id = integerValue(request, "unit_id", 0xFF);
    const int start_address = integerValue(request, "start_address", 0);
    const int end_address = integerValue(request, "end_address", 255);
    const int timeout_ms = integerValue(request, "timeout_ms", 500);

    if (host.empty()) throw std::runtime_error("server_ip is required.");
    if (port < 1 || port > 65535) throw std::runtime_error("server_port must be between 1 and 65535.");
    if (unit_id < 0 || (unit_id > 247 && unit_id != 0xFF)) {
        throw std::runtime_error("unit_id must be between 0 and 247 (or 255 for the TCP default).");
    }
    if (start_address < 0 || end_address > max_modbus_address || end_address < start_address) {
        throw std::runtime_error("Scan addresses must be an ordered range between 0 and 65535.");
    }
    if (end_address - start_address + 1 > max_scan_span) {
        throw std::runtime_error("A register scan is limited to 4096 addresses at a time.");
    }
    if (timeout_ms < 10 || timeout_ms > 5000) {
        throw std::runtime_error("timeout_ms must be between 10 and 5000.");
    }

    Context context(modbus_new_tcp_pi(host.c_str(), std::to_string(port).c_str()));
    if (!context) throw std::runtime_error("Unable to allocate a Modbus TCP context.");
    if (modbus_set_slave(context.get(), unit_id) == -1) {
        throw std::runtime_error(std::string("Unable to configure Modbus Unit ID: ") + modbus_strerror(errno));
    }
    const auto timeout_seconds = static_cast<uint32_t>(timeout_ms / 1000);
    const auto timeout_microseconds = static_cast<uint32_t>((timeout_ms % 1000) * 1000);
    if (modbus_set_response_timeout(context.get(), timeout_seconds, timeout_microseconds) == -1) {
        throw std::runtime_error(std::string("Unable to configure scan timeout: ") + modbus_strerror(errno));
    }
    if (modbus_connect(context.get()) == -1) {
        throw std::runtime_error("Unable to connect to " + host + ":" + std::to_string(port) + ": " +
            modbus_strerror(errno));
    }

    const auto started = std::chrono::steady_clock::now();
    std::size_t request_count = 0;
    const auto coils = scanTable(context.get(), start_address, end_address, TableType::Coils, request_count);
    const auto discrete_inputs = scanTable(
        context.get(), start_address, end_address, TableType::DiscreteInputs, request_count);
    const auto holding = scanTable(
        context.get(), start_address, end_address, TableType::HoldingRegisters, request_count);
    const auto input = scanTable(
        context.get(), start_address, end_address, TableType::InputRegisters, request_count);
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    return {
        {"endpoint", {{"host", host}, {"port", port}, {"unit_id", unit_id}}},
        {"scan_kind", "address_response_probe"},
        {"configured_map_available", false},
        {"configured_map_note", "Standard Modbus responses expose legal addresses, not the device's configured channel map."},
        {"scanned", {{"start_address", start_address}, {"end_address", end_address}}},
        {"coils", tablePayload(coils)},
        {"discrete_inputs", tablePayload(discrete_inputs)},
        {"holding_registers", tablePayload(holding)},
        {"input_registers", tablePayload(input)},
        {"request_count", request_count},
        {"elapsed_ms", elapsed_ms},
    };
}
