//
// Created by kemptonburton on 11/16/2025.
//

#include "modbus_tcp_client_module.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <string>
#include <vector>

namespace {

    struct Mapping {
        int address = 0;
        std::string channel;
    };

    std::string normalizeMappedChannelPath(std::string channel_path) {
        const auto slash_index = channel_path.find('/');
        const auto colon_index = channel_path.find(':');
        if (colon_index != std::string::npos && slash_index != std::string::npos && colon_index < slash_index) {
            channel_path = channel_path.substr(colon_index + 1);
        }

        static const std::unordered_set<std::string> known_fields = {
            "value",
            "commanded_by",
            "timestamp",
            "units",
            "scale",
            "offset",
            "stale_timeout",
            "mapped_channel",
            "record_on_value_change",
            "mean",
            "median",
            "stdev",
            "buffer_size",
            "data_frame",
            "control_policy",
            "control_owner",
            "active_controller"
        };

        const auto last_dot = channel_path.rfind('.');
        if (last_dot != std::string::npos) {
            const auto field = channel_path.substr(last_dot + 1);
            if (known_fields.count(field) > 0) {
                channel_path = channel_path.substr(0, last_dot);
            }
        }

        return channel_path;
    }

    std::optional<std::pair<std::string, std::string>> splitChannelPath(const std::string& channel_path) {
        const std::string normalized_path = normalizeMappedChannelPath(channel_path);
        const auto separator = normalized_path.find('/');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= normalized_path.size()) {
            return std::nullopt;
        }

        return std::make_pair(normalized_path.substr(0, separator), normalized_path.substr(separator + 1));
    }

    std::vector<Mapping> parseMappings(const nlohmann::json& arguments) {
        std::vector<Mapping> mappings;
        if (!arguments.is_object() || !arguments.contains("mappings") || !arguments["mappings"].is_array()) {
            return mappings;
        }

        for (const auto& item : arguments["mappings"]) {
            if (!item.is_object()) {
                continue;
            }

            if (!item.contains("register") || !item["register"].is_number_integer()) {
                continue;
            }

            if (!item.contains("channel") || !item["channel"].is_string()) {
                continue;
            }

            Mapping mapping;
            mapping.address = item["register"].get<int>();
            mapping.channel = normalizeMappedChannelPath(item["channel"].get<std::string>());
            mappings.push_back(std::move(mapping));
        }

        return mappings;
    }

    std::vector<Mapping> parseCoilMappings(const nlohmann::json& arguments) {
        std::vector<Mapping> mappings;
        if (!arguments.is_object()) {
            return mappings;
        }

        if (arguments.contains("mappings") && arguments["mappings"].is_array()) {
            for (const auto& item : arguments["mappings"]) {
                if (!item.is_object()) {
                    continue;
                }

                if (!item.contains("coil") || !item["coil"].is_number_integer()) {
                    continue;
                }

                if (!item.contains("channel") || !item["channel"].is_string()) {
                    continue;
                }

                Mapping mapping;
                mapping.address = item["coil"].get<int>();
                mapping.channel = normalizeMappedChannelPath(item["channel"].get<std::string>());
                mappings.push_back(std::move(mapping));
            }

            return mappings;
        }

        if (arguments.contains("coil") && arguments["coil"].is_number_integer() &&
            arguments.contains("channel") && arguments["channel"].is_string()) {
            Mapping mapping;
            mapping.address = arguments["coil"].get<int>();
            mapping.channel = normalizeMappedChannelPath(arguments["channel"].get<std::string>());
            mappings.push_back(std::move(mapping));
        }

        return mappings;
    }
}

#ifdef _WIN32
    #define EXPORT_API __declspec(dllexport)
#else
    #define EXPORT_API __attribute__((visibility("default")))
#endif

ModbusTCPClientModule::ModbusTCPClientModule(YAML::Node cfg, DARTWIC::API::SDK_API* drtw)
    : BaseModule(cfg, drtw),
    instance_name_(getConfig<std::string>("name")),
    client_(this,
          instance_name_,
          getParameter<std::string>("server_ip"),
          getParameter<int>("server_port", 502),
          static_cast<uint32_t>(getParameter<int>("tv_sec", 3)),
          static_cast<uint32_t>(getParameter<int>("tv_usec", 0))) {

}

ModbusTCPClientModule::~ModbusTCPClientModule() {

}

ModbusTCPClient & ModbusTCPClientModule::getTCPClient() {
    return client_;
}

extern "C" EXPORT_API void onRegistryLoaded(YAML::Node cfg, DARTWIC::API::SDK_API* drtw) {

    std::cout << "hello" << std::endl;


    ///// READ TASK /////
    DARTWIC::API::TaskTypeDefinition read_task_type;
    read_task_type.metadata.task_type = "modbus.read_input_registers";;
    read_task_type.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    read_task_type.metadata.exposed_from = "modbus_tcp_client";
    read_task_type.metadata.expected_module_registry = "modbus_tcp_client";
    read_task_type.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"mappings", nlohmann::json::array()}
    };

    /// START ///
    read_task_type.on_start = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) {

        // SET CHANNEL AUTHORITY
        // go through each channel used by this task and set to observe only autority
        const std::string task_controller = "task:" + task_runtime.getPortalName() + "/" + task_runtime.getTaskName();
        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"observe_only"}
            );
            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                task_controller
            );
            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                task_controller
            );
        }
    };

    /// TASK ///
    read_task_type.on_task = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        /// GET MODULE INSTANCE AND CLIENT
        std::string instance_name = task_runtime.getArguments()["module_instance_name"];
        auto module = drtw->getModuleInstance(instance_name);

        // cast
        auto modbusModule = std::dynamic_pointer_cast<ModbusTCPClientModule>(module);
        if (!modbusModule) {
            return;
        }

        // get reference to client (CLIENT METHODS USED MUST BE THREAD SAFE)
        auto &client = modbusModule->getTCPClient();

        /// IF MODBUS NOT CONNECTED - PASS
        if (!client.isConnected()) {
            return;
        }

        /// GO THROUGH MAPPINGS
        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto value = client.readInputRegister(mapping.address);
            if (!value.has_value()) {
                continue;
            }

            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::VALUE,
                static_cast<double>(*value)
            );
        }
    };

    read_task_type.on_end = [drtw](const DARTWIC::API::TaskTypeDefinition&, DARTWIC::API::TaskRuntime& task_runtime) {
        // SET CHANNEL AUTHORITY
        // set to free once done
        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"free"}
            );
            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                std::string{""}
            );
            drtw->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                std::string{""}
            );
        }
    };

    read_task_type.cleanup = [](DARTWIC::API::TaskRuntime& task_runtime) {
        // no cleanup needed
    };

    // register
    drtw->registerTaskType(read_task_type);


    ///// WRITE TASK /////
    DARTWIC::API::TaskTypeDefinition write_task_type;
    write_task_type.metadata.task_type = "modbus.write_coil";
    write_task_type.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    write_task_type.metadata.exposed_from = "modbus_tcp_client";
    write_task_type.metadata.expected_module_registry = "modbus_tcp_client";
    write_task_type.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"mappings", nlohmann::json::array()}
    };

    write_task_type.on_start = [](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) {
        // nothing for now
    };

    write_task_type.on_task = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        /// GET MODULE INSTANCE AND CLIENT
        std::string instance_name = task_runtime.getArguments()["module_instance_name"];
        auto module = drtw->getModuleInstance(instance_name);

        // cast
        auto modbusModule = std::dynamic_pointer_cast<ModbusTCPClientModule>(module);
        if (!modbusModule) {
            return;
        }

        // get reference to client (CLIENT METHODS USED MUST BE THREAD SAFE)
        auto &client = modbusModule->getTCPClient();

        /// IF MODBUS NOT CONNECTED - PASS
        if (!client.isConnected()) {
            return;
        }

        /// GO THROUGH MAPPINGS
        const auto mappings = parseCoilMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            const double channel_value = drtw->queryChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::VALUE,
                DARTWIC::API::ChannelValue{0.0}
            );

            client.writeCoil(mapping.address, channel_value != 0.0);
        }
    };

    write_task_type.on_end = [](const DARTWIC::API::TaskTypeDefinition&, DARTWIC::API::TaskRuntime& task_runtime) {
        // nothing for now
    };

    write_task_type.cleanup = [](DARTWIC::API::TaskRuntime& task_runtime) {
        // no cleanup needed
    };

    // register
    drtw->registerTaskType(write_task_type);
}

extern "C" EXPORT_API DARTWIC::Modules::BaseModule* createModule(YAML::Node cfg, DARTWIC::API::SDK_API* drtw) {
    return new ModbusTCPClientModule(cfg, drtw);
}


