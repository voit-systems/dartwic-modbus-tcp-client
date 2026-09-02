#ifdef _WIN32
#include <winsock2.h>
#endif

#include "modbus_register_scanner.h"

#include <modbus/modbus.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
class ScanSimulator {
public:
    explicit ScanSimulator(int port) {
        context_ = modbus_new_tcp("127.0.0.1", port);
        mapping_ = modbus_mapping_new(8, 8, 8, 8);
        if (!context_ || !mapping_) throw std::runtime_error("Unable to create scan simulator.");
        worker_ = std::thread([this]() {
            int server_socket = modbus_tcp_listen(context_, 1);
            if (server_socket < 0 || modbus_tcp_accept(context_, &server_socket) < 0) {
                failed_.store(true);
                return;
            }
            uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
            while (true) {
                const int length = modbus_receive(context_, query);
                if (length <= 0) break;
                if (modbus_reply(context_, query, length, mapping_) < 0) {
                    failed_.store(true);
                    break;
                }
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ~ScanSimulator() {
        if (worker_.joinable()) worker_.join();
        if (mapping_) modbus_mapping_free(mapping_);
        if (context_) {
            modbus_close(context_);
            modbus_free(context_);
        }
    }

    bool failed() const { return failed_.load(); }

private:
    modbus_t* context_{};
    modbus_mapping_t* mapping_{};
    std::thread worker_;
    std::atomic<bool> failed_{false};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        constexpr int port = 15029;
        ScanSimulator simulator(port);
        const auto result = scanModbusRegisters({
            {"server_ip", "127.0.0.1"},
            {"server_port", port},
            {"unit_id", 1},
            {"start_address", 0},
            {"end_address", 10},
            {"timeout_ms", 200},
        });

        require(result["scan_kind"] == "address_response_probe", "Expected explicit scan semantics.");
        require(!result["configured_map_available"].get<bool>(),
            "A generic Modbus probe must not claim to expose the configured map.");

        for (const auto* table : {"coils", "discrete_inputs", "holding_registers", "input_registers"}) {
            require(result[table]["supported"].get<bool>(), "Expected the register table to be supported.");
            require(result[table]["available_count"].get<int>() == 8, "Expected eight responding registers.");
            require(result[table]["ranges"].size() == 1, "Expected one contiguous register range.");
            require(result[table]["ranges"][0]["start"].get<int>() == 0, "Expected range to start at zero.");
            require(result[table]["ranges"][0]["end"].get<int>() == 7, "Expected range to end at seven.");
        }
        require(result["request_count"].get<int>() == 44, "Expected one request per address and table.");
        require(!simulator.failed(), "Simulator failed while serving the scan.");
        std::cout << "Modbus register scan integration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
