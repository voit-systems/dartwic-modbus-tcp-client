//
// Created by kemptonburton on 11/16/2025.
//

#include "modbus_tcp_client_module.h"

#include <atomic>
#include <memory>
#include <unordered_set>
#include <string>
#include <vector>

namespace {
    constexpr const char* MODBUS_READ_INPUT_REGISTERS_TASK_TYPE = "modbus.read_input_registers";
    constexpr const char* MODBUS_WRITE_COIL_TASK_TYPE = "modbus.write_coil";
    constexpr const char* MODBUS_READ_RUNTIME_KEY = "modbus_input_register_context";
    constexpr const char* MODBUS_WRITE_RUNTIME_KEY = "modbus_write_coil_context";
    std::atomic<uint64_t> g_modbus_loop_instance_counter{0};

    struct Mapping {
        int address = 0;
        std::string channel;
    };

    struct CachedModbusTaskContext {
        std::string instance_name;
        std::weak_ptr<ModbusTCPClientModule> module;
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

    struct CoilWriteArguments {
        int coil = 0;
        std::string channel;
    };

    std::optional<CoilWriteArguments> parseCoilWriteArguments(const nlohmann::json& arguments) {
        if (!arguments.is_object()) {
            return std::nullopt;
        }

        if (!arguments.contains("coil") || !arguments["coil"].is_number_integer()) {
            return std::nullopt;
        }

        if (!arguments.contains("channel") || !arguments["channel"].is_string()) {
            return std::nullopt;
        }

        CoilWriteArguments parsed;
        parsed.coil = arguments["coil"].get<int>();
        parsed.channel = normalizeMappedChannelPath(arguments["channel"].get<std::string>());
        if (parsed.channel.empty()) {
            return std::nullopt;
        }

        return parsed;
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
      connection_loop_name_("modbus_connection_monitor_" + instance_name_ + "_" + std::to_string(++g_modbus_loop_instance_counter)) {
    if (isInstanceConfig()) {
        client_ = std::make_shared<ModbusTCPClient>(
            this,
            instance_name_,
            getParameter<std::string>("server_ip"),
            getParameter<int>("server_port", 502),
            static_cast<uint32_t>(getParameter<int>("tv_sec", 3)),
            static_cast<uint32_t>(getParameter<int>("tv_usec", 0))
        );

        const std::weak_ptr<ModbusTCPClient> weak_client = client_;
        dartwic->onLoop(connectionLoopName(), [weak_client]() {
            const auto client = weak_client.lock();
            if (!client) {
                return;
            }

            client->maintainConnection();
        });
    }
}

ModbusTCPClientModule::~ModbusTCPClientModule() {
    if (!isInstanceConfig()) {
        return;
    }

    if (dartwic != nullptr) {
        dartwic->removeLoop(connectionLoopName());
    }

    if (client_) {
        client_->disconnect();
    }
}

void ModbusTCPClientModule::onRegistryLoaded() {
    registerTaskTypes();
}

std::optional<int16_t> ModbusTCPClientModule::readInputRegister(int address) {
    if (!client_) {
        return std::nullopt;
    }
    return client_->readInputRegister(address);
}

std::vector<std::optional<int16_t>> ModbusTCPClientModule::readInputRegisters(const std::vector<int>& addresses) {
    if (!client_) {
        return {};
    }
    return client_->readInputRegisters(addresses);
}

bool ModbusTCPClientModule::writeCoil(int address, bool value) {
    if (!client_) {
        return false;
    }

    return client_->writeCoil(address, value);
}

const std::string& ModbusTCPClientModule::getInstanceName() const {
    return instance_name_;
}

bool ModbusTCPClientModule::isInstanceConfig() const {
    return static_cast<bool>(config["registry"]);
}

std::string ModbusTCPClientModule::connectionLoopName() const {
    return connection_loop_name_;
}

void ModbusTCPClientModule::registerTaskTypes() {
    const auto resolve_context = [this](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) -> std::shared_ptr<CachedModbusTaskContext> {
        const auto& arguments = task_runtime.getArguments();
        if (!arguments.is_object()) {
            return nullptr;
        }

        const auto instance_it = arguments.find("module_instance_name");
        if (instance_it == arguments.end() || !instance_it->is_string() || instance_it->get<std::string>().empty()) {
            return nullptr;
        }

        const std::string instance_name = instance_it->get<std::string>();
        auto base_module = dartwic->getModuleInstance(instance_name);
        if (!base_module || base_module->getRegistryName() != definition.metadata.expected_module_registry) {
            return nullptr;
        }
        auto module = std::dynamic_pointer_cast<ModbusTCPClientModule>(base_module);
        if (!module) {
            return nullptr;
        }

        auto cached = std::make_shared<CachedModbusTaskContext>();
        cached->instance_name = instance_name;
        cached->module = module;
        return cached;
    };

    DARTWIC::API::TaskTypeDefinition read_task_type;
    read_task_type.metadata.task_type = MODBUS_READ_INPUT_REGISTERS_TASK_TYPE;
    read_task_type.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    read_task_type.metadata.exposed_from = "modbus_tcp_client";
    read_task_type.metadata.expected_module_registry = "modbus_tcp_client";
    read_task_type.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"mappings", nlohmann::json::array()}
    };

    read_task_type.on_start = [resolve_context, this](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) {
        const auto cached = resolve_context(definition, task_runtime);
        if (cached) {
            task_runtime.setTypedRuntimeContext(MODBUS_READ_RUNTIME_KEY, cached);
        }

        const std::string task_controller = "task:" + task_runtime.getPortalName() + "/" + task_runtime.getTaskName();
        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"observe_only"}
            );
            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                task_controller
            );
            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                task_controller
            );
        }
    };

    read_task_type.on_task = [this, resolve_context](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        (void)elapsed_seconds;
        auto cached = task_runtime.getTypedRuntimeContext<CachedModbusTaskContext>(MODBUS_READ_RUNTIME_KEY);
        if (!cached) {
            cached = resolve_context(definition, task_runtime);
            if (!cached) {
                return;
            }
            task_runtime.setTypedRuntimeContext(MODBUS_READ_RUNTIME_KEY, cached);
        }

        auto module = cached->module.lock();
        if (!module) {
            task_runtime.removeRuntimeContext(MODBUS_READ_RUNTIME_KEY);
            return;
        }

        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto value = module->readInputRegister(mapping.address);
            if (!value.has_value()) {
                continue;
            }

            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::VALUE,
                static_cast<double>(*value)
            );
        }
    };

    read_task_type.on_end = [this](const DARTWIC::API::TaskTypeDefinition&, DARTWIC::API::TaskRuntime& task_runtime) {
        const auto mappings = parseMappings(task_runtime.getArguments());
        for (const auto& mapping : mappings) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"free"}
            );
            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                std::string{""}
            );
            dartwic->upsertChannelField(
                channel_path->first,
                channel_path->second,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                std::string{""}
            );
        }

        task_runtime.removeRuntimeContext(MODBUS_READ_RUNTIME_KEY);
    };

    read_task_type.cleanup = [](DARTWIC::API::TaskRuntime& task_runtime) {
        task_runtime.removeRuntimeContext(MODBUS_READ_RUNTIME_KEY);
    };

    dartwic->registerTaskType(read_task_type);

    DARTWIC::API::TaskTypeDefinition write_task_type;
    write_task_type.metadata.task_type = MODBUS_WRITE_COIL_TASK_TYPE;
    write_task_type.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    write_task_type.metadata.exposed_from = "modbus_tcp_client";
    write_task_type.metadata.expected_module_registry = "modbus_tcp_client";
    write_task_type.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"coil", 0},
        {"channel", ""}
    };

    write_task_type.on_start = [resolve_context](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) {
        const auto cached = resolve_context(definition, task_runtime);
        if (cached) {
            task_runtime.setTypedRuntimeContext(MODBUS_WRITE_RUNTIME_KEY, cached);
        }
    };

    write_task_type.on_task = [this, resolve_context](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        (void)elapsed_seconds;
        auto cached = task_runtime.getTypedRuntimeContext<CachedModbusTaskContext>(MODBUS_WRITE_RUNTIME_KEY);
        if (!cached) {
            cached = resolve_context(definition, task_runtime);
            if (!cached) {
                return;
            }
            task_runtime.setTypedRuntimeContext(MODBUS_WRITE_RUNTIME_KEY, cached);
        }

        auto module = cached->module.lock();
        if (!module) {
            task_runtime.removeRuntimeContext(MODBUS_WRITE_RUNTIME_KEY);
            return;
        }

        const auto coil_write = parseCoilWriteArguments(task_runtime.getArguments());
        if (!coil_write.has_value()) {
            return;
        }

        const auto channel_path = splitChannelPath(coil_write->channel);
        if (!channel_path.has_value()) {
            return;
        }

        const double channel_value = dartwic->queryChannelField(
            channel_path->first,
            channel_path->second,
            DARTWIC::API::ChannelField::VALUE,
            DARTWIC::API::ChannelValue{0.0}
        );

        module->writeCoil(coil_write->coil, channel_value != 0.0);
    };

    write_task_type.on_end = [](const DARTWIC::API::TaskTypeDefinition&, DARTWIC::API::TaskRuntime& task_runtime) {
        task_runtime.removeRuntimeContext(MODBUS_WRITE_RUNTIME_KEY);
    };

    write_task_type.cleanup = [](DARTWIC::API::TaskRuntime& task_runtime) {
        task_runtime.removeRuntimeContext(MODBUS_WRITE_RUNTIME_KEY);
    };

    dartwic->registerTaskType(write_task_type);
}

extern "C" EXPORT_API DARTWIC::Modules::BaseModule* createModule(YAML::Node cfg, DARTWIC::API::SDK_API* drtw) {
    return new ModbusTCPClientModule(cfg, drtw);
}
