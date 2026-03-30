//
// Created by kemptonburton on 11/16/2025.
//

#ifndef MODBUS_TCP_CLIENT_MODULE_H
#define MODBUS_TCP_CLIENT_MODULE_H

#include <BaseModule.h>
#include <memory>
#include <modbus_tcp_client.h>
#include <optional>

class ModbusTCPClientModule : public DARTWIC::Modules::BaseModule {
public:
    ModbusTCPClientModule(YAML::Node cfg, DARTWIC::API::SDK_API* drtw);

    void onRegistryLoaded() override;

    std::optional<int16_t> readInputRegister(int address);
    std::vector<std::optional<int16_t>> readInputRegisters(const std::vector<int>& addresses);
    bool writeCoil(int address, bool value);
    bool isConnected() const;
    const std::string& getInstanceName() const;

    ~ModbusTCPClientModule() override;

private:
    bool isInstanceConfig() const;
    void registerTaskTypes();
    std::string connectionLoopName() const;

    std::string instance_name_;
    std::string connection_loop_name_;
    std::shared_ptr<ModbusTCPClient> client_;
};

#endif //MODBUS_TCP_CLIENT_MODULE_H
