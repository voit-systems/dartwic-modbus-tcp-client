//
// Created by kemptonburton on 11/16/2025.
//

#ifndef MODBUS_TCP_CLIENT_MODULE_H
#define MODBUS_TCP_CLIENT_MODULE_H

#include <modules/BaseModule.h>
#include <memory>
#include <modbus_tcp_client.h>
#include <mutex>
#include <optional>
#include <thread>

class ModbusTCPClientModule : public DARTWIC::Modules::BaseModule {
public:
    ModbusTCPClientModule(nlohmann::json cfg, DARTWIC::API::SDK_API* drtw);

    ModbusTCPClient& getTCPClient();
    std::mutex& connectionMutex();
    void monitorConnection();

    ~ModbusTCPClientModule() override = default;

private:
    std::string instance_name_;
    std::mutex connection_mutex_;
    ModbusTCPClient client_;
    std::jthread connection_monitor_thread_;
};

#endif //MODBUS_TCP_CLIENT_MODULE_H
