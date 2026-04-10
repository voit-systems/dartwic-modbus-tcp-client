//
// Created by kemptonburton on 11/16/2025.
//

#ifndef MODBUS_TCP_CLIENT_MODULE_H
#define MODBUS_TCP_CLIENT_MODULE_H

#include <modules/BaseModule.h>
#include <memory>
#include <modbus_tcp_client.h>
#include <optional>

class ModbusTCPClientModule : public DARTWIC::Modules::BaseModule {
public:
    ModbusTCPClientModule(nlohmann::json cfg, DARTWIC::API::SDK_API* drtw);

    ModbusTCPClient& getTCPClient();

    ~ModbusTCPClientModule() override;

private:
    std::string instance_name_;
    ModbusTCPClient client_;
};

#endif //MODBUS_TCP_CLIENT_MODULE_H
