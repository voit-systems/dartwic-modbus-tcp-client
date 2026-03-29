//
// Created by kemptonburton on 11/16/2025.
//

#ifndef MODBUS_TCP_CLIENT_H
#define MODBUS_TCP_CLIENT_H

#include <modbus/modbus.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ModbusTCPClientModule;

class ModbusTCPClient {
public:
    ModbusTCPClient(ModbusTCPClientModule* module,
        std::string instance_name,
        std::string server_ip,
        int server_port,
        uint32_t tv_sec,
        uint32_t tv_usec);

    ~ModbusTCPClient();

    bool ensureConnected();
    void maintainConnection();
    void disconnect();
    std::optional<int16_t> readInputRegister(int address);
    std::vector<std::optional<int16_t>> readInputRegisters(const std::vector<int>& addresses);

    const std::string& getInstanceName() const;

private:
    bool connectUnlocked();
    void disconnectUnlocked();
    void handleDisconnectUnlocked(const std::string& error_message);
    void onError(const char* error);
    void setConnected(double connected_value);

    ModbusTCPClientModule* module_;
    std::string instance_name_;
    std::string server_ip_;
    int server_port_{};
    modbus_t* ctx_{};
    std::mutex ctx_lock_;
    uint32_t tv_sec_{};
    uint32_t tv_usec_{};
    bool connected_ = false;
    bool has_published_connection_state_ = false;
};

#endif //MODBUS_TCP_CLIENT_H
