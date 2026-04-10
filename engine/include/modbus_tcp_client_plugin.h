#ifndef MODBUS_TCP_CLIENT_PLUGIN_H
#define MODBUS_TCP_CLIENT_PLUGIN_H

#include <plugins/BasePlugin.h>

#include "modbus_tcp_client_module.h"

class ModbusTCPClientPlugin : public DARTWIC::Plugins::BasePlugin {
public:
    ModbusTCPClientPlugin(nlohmann::json cfg, DARTWIC::API::SDK_API* drtw)
        : BasePlugin(std::move(cfg), drtw) {}

    void onPluginLoaded() override;

    std::vector<DARTWIC::Plugins::PluginModuleType> getModuleTypes() const override {
        return {
            {"modbus_tcp_client", "module_config.json"}
        };
    }

    DARTWIC::Modules::BaseModule* createModule(
        const std::string& module_type_id,
        nlohmann::json cfg,
        DARTWIC::API::SDK_API* drtw
    ) override;
};

#endif //MODBUS_TCP_CLIENT_PLUGIN_H
