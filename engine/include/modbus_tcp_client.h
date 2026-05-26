//
// Created by kemptonburton on 11/16/2025.
//

#ifndef MODBUS_TCP_CLIENT_H
#define MODBUS_TCP_CLIENT_H

#include <modbus/modbus.h>
#include <atomic>
#include <cstdint>
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

    bool isConnected() const;

    std::optional<int16_t> readInputRegister(int address);
    std::optional<std::vector<int16_t>> readInputRegisterBlock(int start_address, int count);
    std::vector<std::optional<int16_t>> readInputRegisters(const std::vector<int>& addresses);
    bool writeCoil(int address, bool value);
    bool writeCoilBlock(int start_address, const std::vector<uint8_t>& values);

    std::optional<uint8_t> readCoil(int address);
    std::optional<std::vector<uint8_t>> readCoilBlock(int start_address, int count);
    bool writeHoldingRegisterBlock(int start_address, const std::vector<uint16_t>& values);
    std::optional<std::vector<uint16_t>> readHoldingRegisterBlock(int start_address, int count);

private:
    bool connect();
    void checkConnection();
    void disconnect();
    void setConnected(double connected_value);
    void configureConnectedChannel();
    void closeContextAndSetDisconnected();
    void handleOperationFailure(const std::string& operation_name);

    ModbusTCPClientModule* module_;
    std::string instance_name_;
    std::string server_ip_;
    int server_port_{};
    modbus_t* ctx_{};
    std::mutex ctx_lock_;
    uint32_t tv_sec_{};
    uint32_t tv_usec_{};
    std::atomic<bool> connected_{false};
};

#endif //MODBUS_TCP_CLIENT_H
