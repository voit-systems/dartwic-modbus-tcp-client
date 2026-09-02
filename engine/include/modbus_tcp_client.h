#ifndef MODBUS_TCP_CLIENT_H
#define MODBUS_TCP_CLIENT_H

#include <modbus/modbus.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class ModbusTCPClientModule;

// One configured read task and one configured write task share this connection.
// The module serializes all protocol transactions.
class ModbusTCPClient {
public:
    ModbusTCPClient(ModbusTCPClientModule* module,
        std::string instance_name,
        std::string server_ip,
        int server_port,
        int unit_id,
        uint32_t timeout_seconds,
        uint32_t timeout_microseconds,
        std::string event_system,
        std::string event_subsystem);
    ~ModbusTCPClient();

    bool ensureConnected();
    void monitorConnection();
    bool isConnected() const noexcept { return connected_; }
    void disconnect();

    bool readInputRegisters(int start_address, std::vector<uint16_t>& values);
    bool readDiscreteInputs(int start_address, std::vector<uint8_t>& values);
    bool readCoils(int start_address, std::vector<uint8_t>& values);
    bool readHoldingRegisters(int start_address, std::vector<uint16_t>& values);
    bool writeCoils(int start_address, const std::vector<uint8_t>& values);
    bool writeHoldingRegisters(int start_address, const std::vector<uint16_t>& values);

    uint64_t reconnectCount() const noexcept { return reconnect_count_; }
    uint64_t failureCount() const noexcept { return failure_count_; }

private:
    bool connect();
    void setConnected(bool connected);
    void setProtocolHealthy(bool healthy);
    void markProtocolResponsive();
    void scheduleReconnect();
    void configureConnectedChannel();
    void closeContextAndSetDisconnected();
    void handleOperationFailure(const std::string& operation_name);
    void publishConnectionError(const std::string& error_message);
    void resolveConnectionError();
    void publishOperationError(const std::string& operation_name, const std::string& error_message);

    ModbusTCPClientModule* module_;
    std::string instance_name_;
    std::string server_ip_;
    int server_port_{};
    int unit_id_{1};
    modbus_t* context_{};
    uint32_t timeout_seconds_{};
    uint32_t timeout_microseconds_{};
    std::string event_system_;
    std::string event_subsystem_;
    bool connected_{false};
    bool protocol_healthy_{false};
    bool connection_state_published_{false};
    uint64_t reconnect_count_{0};
    uint64_t failure_count_{0};
    uint32_t consecutive_connection_failures_{0};
    std::chrono::steady_clock::time_point next_reconnect_attempt_{};
    std::chrono::steady_clock::time_point last_connection_error_publication_{};
    std::chrono::steady_clock::time_point last_operation_error_publication_{};
    std::string last_connection_error_message_;
    std::string connection_error_event_id_;
};

#endif
