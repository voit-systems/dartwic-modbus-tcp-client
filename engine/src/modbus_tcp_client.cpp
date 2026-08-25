#include "modbus_tcp_client.h"

#include "modbus_tcp_client_module.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace {
std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
        return !is_space(static_cast<unsigned char>(character));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
        return !is_space(static_cast<unsigned char>(character));
    }).base(), value.end());
    return value;
}

bool parsePort(const std::string& value, int& port) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    })) return false;
    char* parse_end = nullptr;
    const long parsed_port = std::strtol(value.c_str(), &parse_end, 10);
    if (parse_end == value.c_str() || *parse_end != '\0' || parsed_port <= 0 || parsed_port > 65535) return false;
    port = static_cast<int>(parsed_port);
    return true;
}

std::pair<std::string, int> normalizeEndpoint(std::string server_ip, int server_port) {
    server_ip = trim(std::move(server_ip));
    const auto scheme = server_ip.find("://");
    if (scheme != std::string::npos) server_ip = server_ip.substr(scheme + 3);
    if (std::count(server_ip.begin(), server_ip.end(), ':') == 1) {
        const auto separator = server_ip.rfind(':');
        int parsed_port = server_port;
        if (separator != std::string::npos && parsePort(trim(server_ip.substr(separator + 1)), parsed_port)) {
            server_port = parsed_port;
            server_ip = server_ip.substr(0, separator);
        }
    }
    return {trim(std::move(server_ip)), server_port};
}

bool isConnectionLossError(int error_code) {
    return error_code == ETIMEDOUT || error_code == ECONNRESET || error_code == ECONNREFUSED ||
        error_code == ENOTCONN || error_code == EPIPE;
}
} // namespace

ModbusTCPClient::ModbusTCPClient(ModbusTCPClientModule* module,
    std::string instance_name,
    std::string server_ip,
    int server_port,
    uint32_t timeout_seconds,
    uint32_t timeout_microseconds)
    : module_(module),
      instance_name_(std::move(instance_name)),
      server_ip_(std::move(server_ip)),
      server_port_(server_port),
      timeout_seconds_(timeout_seconds),
      timeout_microseconds_(timeout_microseconds) {
    auto endpoint = normalizeEndpoint(std::move(server_ip_), server_port_);
    server_ip_ = std::move(endpoint.first);
    server_port_ = endpoint.second;
    configureConnectedChannel();
    setConnected(false);
}

ModbusTCPClient::~ModbusTCPClient() {
    disconnect();
}

void ModbusTCPClient::configureConnectedChannel() {
    const std::string channel = instance_name_ + ".info.connected";
    module_->dartwic->upsertChannelField(channel, DARTWIC::API::ChannelField::VALUE, 0.0,
        DARTWIC::API::ChannelStorage::Fixed);
    module_->dartwic->upsertChannelField(channel, DARTWIC::API::ChannelField::UNITS, std::string{"bool"},
        DARTWIC::API::ChannelStorage::Fixed);
}

void ModbusTCPClient::setConnected(bool connected) {
    connected_ = connected;
    module_->dartwic->upsertChannelField(instance_name_ + ".info.connected",
        DARTWIC::API::ChannelField::VALUE, connected ? 1.0 : 0.0, DARTWIC::API::ChannelStorage::Fixed);
}

bool ModbusTCPClient::ensureConnected() {
    if (connected_ && context_ != nullptr) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_reconnect_attempt_) return false;
    next_reconnect_attempt_ = now + std::chrono::seconds(1);
    return connect();
}

bool ModbusTCPClient::connect() {
    closeContextAndSetDisconnected();
    if (server_ip_.empty() || server_port_ <= 0 || server_port_ > 65535) {
        publishConnectionError("Invalid Modbus TCP endpoint.");
        return false;
    }
    const std::string service = std::to_string(server_port_);
    context_ = modbus_new_tcp_pi(server_ip_.c_str(), service.c_str());
    if (context_ == nullptr) {
        publishConnectionError("Unable to create Modbus TCP context.");
        return false;
    }
    modbus_set_response_timeout(context_, timeout_seconds_, timeout_microseconds_);
    if (modbus_connect(context_) == -1) {
        publishConnectionError(modbus_strerror(errno));
        closeContextAndSetDisconnected();
        return false;
    }
    ++reconnect_count_;
    setConnected(true);
    return true;
}

void ModbusTCPClient::closeContextAndSetDisconnected() {
    if (context_ != nullptr) {
        modbus_close(context_);
        modbus_free(context_);
        context_ = nullptr;
    }
    setConnected(false);
}

void ModbusTCPClient::disconnect() {
    closeContextAndSetDisconnected();
}

void ModbusTCPClient::handleOperationFailure(const std::string& operation_name) {
    ++failure_count_;
    const int error_code = errno;
    const std::string message = modbus_strerror(error_code);
    if (isConnectionLossError(error_code)) {
        publishConnectionError(message);
        closeContextAndSetDisconnected();
    } else {
        publishOperationError(operation_name, message);
    }
}

void ModbusTCPClient::publishConnectionError(const std::string& error_message) {
    const auto now = std::chrono::steady_clock::now();
    if (last_error_publication_.time_since_epoch().count() != 0 && now - last_error_publication_ < std::chrono::seconds(2)) return;
    last_error_publication_ = now;
    module_->dartwic->consoleError(
        "MODBUS CONNECTION ERROR [" + instance_name_ + "]",
        error_message + " [" + server_ip_ + ":" + std::to_string(server_port_) + "]",
        {instance_name_ + ".info.connected"},
        "Verify the endpoint and device availability.",
        3
    );
}

void ModbusTCPClient::publishOperationError(const std::string& operation_name, const std::string& error_message) {
    const auto now = std::chrono::steady_clock::now();
    if (last_error_publication_.time_since_epoch().count() != 0 && now - last_error_publication_ < std::chrono::seconds(2)) return;
    last_error_publication_ = now;
    module_->dartwic->consoleError(
        "MODBUS TRANSACTION ERROR [" + instance_name_ + "]",
        operation_name + ": " + error_message,
        {instance_name_ + ".info.connected"},
        "Verify mapped addresses, device state, and timeout settings.",
        3
    );
}

bool ModbusTCPClient::readInputRegisters(int start_address, std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    const int result = modbus_read_input_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_input_registers"); return false; }
    return result == static_cast<int>(values.size());
}

bool ModbusTCPClient::readCoils(int start_address, std::vector<uint8_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    const int result = modbus_read_bits(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_coils"); return false; }
    return result == static_cast<int>(values.size());
}

bool ModbusTCPClient::readHoldingRegisters(int start_address, std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    const int result = modbus_read_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_holding_registers"); return false; }
    return result == static_cast<int>(values.size());
}

bool ModbusTCPClient::writeCoils(int start_address, const std::vector<uint8_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    const int result = modbus_write_bits(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("write_coils"); return false; }
    return result == static_cast<int>(values.size());
}

bool ModbusTCPClient::writeHoldingRegisters(int start_address, const std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    const int result = modbus_write_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("write_holding_registers"); return false; }
    return result == static_cast<int>(values.size());
}
