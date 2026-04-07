//
// Created by kemptonburton on 11/16/2025.
//

#include "modbus_tcp_client_module.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <string>
#include <vector>

namespace {

    enum class RegisterType {
        Coil,
        HoldingRegister
    };

    struct Mapping {
        int address = 0;
        std::string channel;
        RegisterType register_type = RegisterType::Coil;
    };

    struct ResolvedMapping {
        int address = 0;
        std::string portal;
        std::string channel;
        std::string state_channel;
        RegisterType register_type = RegisterType::Coil;
    };

    struct WriteMappingBlock {
        RegisterType register_type = RegisterType::Coil;
        int start_address = 0;
        std::vector<ResolvedMapping> mappings;
        std::vector<uint8_t> coil_values;
        std::vector<uint8_t> last_written_coil_values;
        std::vector<uint16_t> holding_register_values;
        std::vector<uint16_t> last_written_holding_register_values;
        bool write_initialized = false;
        bool needs_readback = false;
    };

    struct ReadMappingBlock {
        int start_address = 0;
        std::vector<ResolvedMapping> mappings;
    };

    struct WriteTaskRuntimeContext {
        std::shared_ptr<ModbusTCPClientModule> modbus_module;
        std::vector<WriteMappingBlock> blocks;
        double readback_interval_seconds = 0.5;
        bool periodic_readback_enabled = true;
        std::chrono::steady_clock::time_point next_readback_time = std::chrono::steady_clock::now();
    };

    std::optional<RegisterType> parseRegisterType(const nlohmann::json& item) {
        if (item.contains("register_type") && item["register_type"].is_string()) {
            const auto register_type = item["register_type"].get<std::string>();
            if (register_type == "coil") {
                return RegisterType::Coil;
            }

            if (register_type == "holding_register" || register_type == "holding_registers") {
                return RegisterType::HoldingRegister;
            }
        }

        return std::nullopt;
    }

    std::string registerTypeToString(RegisterType register_type) {
        switch (register_type) {
            case RegisterType::HoldingRegister:
                return "holding_register";
            case RegisterType::Coil:
            default:
                return "coil";
        }
    }

    uint16_t channelValueToRegister(double value) {
        if (value <= 0.0) {
            return 0;
        }

        if (value >= static_cast<double>((std::numeric_limits<uint16_t>::max)())) {
            return (std::numeric_limits<uint16_t>::max)();
        }

        return static_cast<uint16_t>(value);
    }

    double getWriteReadbackIntervalSeconds(const nlohmann::json& arguments) {
        if (!arguments.is_object()) {
            return 0.5;
        }

        if (arguments.contains("readback_interval_seconds") && arguments["readback_interval_seconds"].is_number()) {
            return arguments["readback_interval_seconds"].get<double>();
        }

        if (arguments.contains("readback_interval") && arguments["readback_interval"].is_number()) {
            return arguments["readback_interval"].get<double>();
        }

        return 0.5;
    }


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

    std::vector<Mapping> parseWriteMappings(const nlohmann::json& arguments) {
        std::vector<Mapping> mappings;
        if (!arguments.is_object()) {
            return mappings;
        }

        if (arguments.contains("mappings") && arguments["mappings"].is_array()) {
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

                const auto register_type = parseRegisterType(item);
                if (!register_type.has_value()) {
                    continue;
                }

                Mapping mapping;
                mapping.address = item["register"].get<int>();
                mapping.channel = normalizeMappedChannelPath(item["channel"].get<std::string>());
                mapping.register_type = *register_type;
                mappings.push_back(std::move(mapping));
            }

            return mappings;
        }

        if (arguments.contains("register") && arguments["register"].is_number_integer() &&
            arguments.contains("channel") && arguments["channel"].is_string()) {
            const auto register_type = parseRegisterType(arguments);
            if (!register_type.has_value()) {
                return mappings;
            }

            Mapping mapping;
            mapping.address = arguments["register"].get<int>();
            mapping.channel = normalizeMappedChannelPath(arguments["channel"].get<std::string>());
            mapping.register_type = *register_type;
            mappings.push_back(std::move(mapping));
        }

        return mappings;
    }

    std::vector<ResolvedMapping> resolveRegisterMappings(const nlohmann::json& arguments) {
        std::vector<ResolvedMapping> resolved;
        for (const auto& mapping : parseMappings(arguments)) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            resolved.push_back(ResolvedMapping{
                .address = mapping.address,
                .portal = channel_path->first,
                .channel = channel_path->second,
                .state_channel = {}
            });
        }

        std::sort(resolved.begin(), resolved.end(), [](const ResolvedMapping& lhs, const ResolvedMapping& rhs) {
            return lhs.address < rhs.address;
        });

        return resolved;
    }

    std::vector<ResolvedMapping> resolveWriteMappings(const nlohmann::json& arguments) {
        std::vector<ResolvedMapping> resolved;
        for (const auto& mapping : parseWriteMappings(arguments)) {
            const auto channel_path = splitChannelPath(mapping.channel);
            if (!channel_path.has_value()) {
                continue;
            }

            resolved.push_back(ResolvedMapping{
                .address = mapping.address,
                .portal = channel_path->first,
                .channel = channel_path->second,
                .state_channel = channel_path->second + "_state",
                .register_type = mapping.register_type
            });
        }

        std::sort(resolved.begin(), resolved.end(), [](const ResolvedMapping& lhs, const ResolvedMapping& rhs) {
            if (lhs.register_type != rhs.register_type) {
                return registerTypeToString(lhs.register_type) < registerTypeToString(rhs.register_type);
            }

            return lhs.address < rhs.address;
        });

        return resolved;
    }

    std::vector<ReadMappingBlock> buildReadMappingBlocks(const std::vector<ResolvedMapping>& mappings) {
        std::vector<ReadMappingBlock> blocks;

        size_t index = 0;
        while (index < mappings.size()) {
            const int block_start = mappings[index].address;
            size_t block_end = index + 1;

            while (block_end < mappings.size() && mappings[block_end].address == mappings[block_end - 1].address + 1) {
                ++block_end;
            }

            ReadMappingBlock block;
            block.start_address = block_start;
            block.mappings.assign(mappings.begin() + static_cast<std::ptrdiff_t>(index), mappings.begin() + static_cast<std::ptrdiff_t>(block_end));
            blocks.push_back(std::move(block));
            index = block_end;
        }

        return blocks;
    }

    std::vector<WriteMappingBlock> buildWriteMappingBlocks(const std::vector<ResolvedMapping>& mappings) {
        std::vector<WriteMappingBlock> blocks;

        size_t index = 0;
        while (index < mappings.size()) {
            const RegisterType block_type = mappings[index].register_type;
            const int block_start = mappings[index].address;
            size_t block_end = index + 1;

            while (block_end < mappings.size() &&
                   mappings[block_end].register_type == block_type &&
                   mappings[block_end].address == mappings[block_end - 1].address + 1) {
                ++block_end;
            }

            WriteMappingBlock block;
            block.register_type = block_type;
            block.start_address = block_start;
            block.mappings.assign(mappings.begin() + static_cast<std::ptrdiff_t>(index), mappings.begin() + static_cast<std::ptrdiff_t>(block_end));
            if (block_type == RegisterType::HoldingRegister) {
                block.holding_register_values.resize(block.mappings.size(), 0);
                block.last_written_holding_register_values.resize(block.mappings.size(), 0);
            } else {
                block.coil_values.resize(block.mappings.size(), 0);
                block.last_written_coil_values.resize(block.mappings.size(), 0);
            }
            blocks.push_back(std::move(block));
            index = block_end;
        }

        return blocks;
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
          static_cast<uint32_t>(getParameter<int>("tv_sec", 0)),
          static_cast<uint32_t>(getParameter<int>("tv_usec", 200000))) {

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

        // SET CHANNEL AUTHORITY and STALE
        // go through each channel used by this task and set to observe only autority
        const std::string task_controller = "task:" + task_runtime.getPortalName() + "/" + task_runtime.getTaskName();
        const auto mappings = resolveRegisterMappings(task_runtime.getArguments());
        task_runtime.setTypedRuntimeContext("resolved_register_mappings", std::make_shared<std::vector<ResolvedMapping>>(mappings));
        task_runtime.setTypedRuntimeContext("read_mapping_blocks", std::make_shared<std::vector<ReadMappingBlock>>(buildReadMappingBlocks(mappings)));

        const std::string instance_name = task_runtime.getArguments().value("module_instance_name", std::string{""});
        auto module = drtw->getModuleInstance(instance_name);
        auto modbusModule = std::dynamic_pointer_cast<ModbusTCPClientModule>(module);
        if (modbusModule) {
            task_runtime.setTypedRuntimeContext("read_modbus_module", modbusModule);
        }

        for (const auto& mapping : mappings) {
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"observe_only"}
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                task_controller
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                task_controller
            );

            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::STALE_TIMEOUT,
                2.0
            );
        }
    };

    /// TASK ///
    read_task_type.on_task = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        auto modbusModule = task_runtime.getTypedRuntimeContext<ModbusTCPClientModule>("read_modbus_module");
        if (!modbusModule) {
            return;
        }

        // get reference to client (CLIENT METHODS USED MUST BE THREAD SAFE)
        auto &client = modbusModule->getTCPClient();

        /// IF MODBUS NOT CONNECTED - PASS
        if (!client.isConnected()) {
            return;
        }

        auto blocks = task_runtime.getTypedRuntimeContext<std::vector<ReadMappingBlock>>("read_mapping_blocks");
        if (!blocks) {
            return;
        }

        for (const auto& block : *blocks) {
            const int count = static_cast<int>(block.mappings.size());
            const auto values = client.readInputRegisterBlock(block.start_address, count);
            if (values.has_value()) {
                for (size_t offset = 0; offset < static_cast<size_t>(count); ++offset) {
                    const auto& mapping = block.mappings[offset];
                    drtw->upsertChannelField(
                        mapping.portal,
                        mapping.channel,
                        DARTWIC::API::ChannelField::VALUE,
                        static_cast<double>((*values)[offset])
                    );
                }
            }
        }
    };

    read_task_type.on_end = [drtw](const DARTWIC::API::TaskTypeDefinition&, DARTWIC::API::TaskRuntime& task_runtime) {
        // SET CHANNEL AUTHORITY
        // set to free once done
        auto mappings = task_runtime.getTypedRuntimeContext<std::vector<ResolvedMapping>>("resolved_register_mappings");
        if (!mappings) {
            mappings = std::make_shared<std::vector<ResolvedMapping>>(resolveRegisterMappings(task_runtime.getArguments()));
        }

        for (const auto& mapping : *mappings) {
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"free"}
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                std::string{""}
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
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
    write_task_type.metadata.task_type = "modbus.write";
    write_task_type.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    write_task_type.metadata.exposed_from = "modbus_tcp_client";
    write_task_type.metadata.expected_module_registry = "modbus_tcp_client";
    write_task_type.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"readback_interval_seconds", 0.5},
        {"mappings", nlohmann::json::array()}
    };

    write_task_type.on_start = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime) {

        // SET CHANNEL AUTHORITY and STALE for write state channels
        // go through each channel used by this task and set to observe only autority
        const std::string task_controller = "task:" + task_runtime.getPortalName() + "/" + task_runtime.getTaskName();
        const auto mappings = resolveWriteMappings(task_runtime.getArguments());
        task_runtime.setTypedRuntimeContext("resolved_write_mappings", std::make_shared<std::vector<ResolvedMapping>>(mappings));

        auto write_context = std::make_shared<WriteTaskRuntimeContext>();
        write_context->blocks = buildWriteMappingBlocks(mappings);
        write_context->readback_interval_seconds = getWriteReadbackIntervalSeconds(task_runtime.getArguments());
        write_context->periodic_readback_enabled = write_context->readback_interval_seconds > 0.0;
        write_context->next_readback_time = std::chrono::steady_clock::now();

        const std::string instance_name = task_runtime.getArguments().value("module_instance_name", std::string{""});
        auto module = drtw->getModuleInstance(instance_name);
        auto modbusModule = std::dynamic_pointer_cast<ModbusTCPClientModule>(module);
        if (modbusModule) {
            write_context->modbus_module = modbusModule;
        }
        task_runtime.setTypedRuntimeContext("write_task_runtime_context", write_context);

        for (const auto& mapping : mappings) {

            //state channel
            drtw->upsertChannelField(
                mapping.portal,
                mapping.state_channel,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"observe_only"}
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.state_channel,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                task_controller
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.state_channel,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                task_controller
            );

            drtw->upsertChannelField(
                mapping.portal,
                mapping.state_channel,
                DARTWIC::API::ChannelField::STALE_TIMEOUT,
                write_context->periodic_readback_enabled ? write_context->readback_interval_seconds * 2.0 : 0.0
            );

            //channel
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_POLICY,
                std::string{"free"}
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::CONTROL_OWNER,
                task_controller
            );
            drtw->upsertChannelField(
                mapping.portal,
                mapping.channel,
                DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
                task_controller
            );

        }

    };

    write_task_type.on_task = [drtw](const DARTWIC::API::TaskTypeDefinition& definition, DARTWIC::API::TaskRuntime& task_runtime, double elapsed_seconds) {
        auto write_context = task_runtime.getTypedRuntimeContext<WriteTaskRuntimeContext>("write_task_runtime_context");
        if (!write_context || !write_context->modbus_module) {
            return;
        }

        // get reference to client (CLIENT METHODS USED MUST BE THREAD SAFE)
        auto &client = write_context->modbus_module->getTCPClient();

        /// IF MODBUS NOT CONNECTED - PASS
        if (!client.isConnected()) {
            return;
        }

        for (auto& block : write_context->blocks) {
            if (block.register_type == RegisterType::HoldingRegister) {
                bool should_write = !block.write_initialized;
                for (size_t index = 0; index < block.mappings.size(); ++index) {
                    const auto& mapping = block.mappings[index];
                    block.holding_register_values[index] = channelValueToRegister(drtw->queryChannelField(
                        mapping.portal,
                        mapping.channel,
                        DARTWIC::API::ChannelField::VALUE,
                        DARTWIC::API::ChannelValue{0.0}
                    ));
                    should_write = should_write || block.holding_register_values[index] != block.last_written_holding_register_values[index];
                }
                if (should_write && client.writeHoldingRegisterBlock(block.start_address, block.holding_register_values)) {
                    block.last_written_holding_register_values = block.holding_register_values;
                    block.write_initialized = true;
                    block.needs_readback = true;
                }
            } else {
                bool should_write = !block.write_initialized;
                for (size_t index = 0; index < block.mappings.size(); ++index) {
                    const auto& mapping = block.mappings[index];
                    block.coil_values[index] = drtw->queryChannelField(
                        mapping.portal,
                        mapping.channel,
                        DARTWIC::API::ChannelField::VALUE,
                        DARTWIC::API::ChannelValue{0.0}
                    ) != 0.0 ? 1 : 0;
                    should_write = should_write || block.coil_values[index] != block.last_written_coil_values[index];
                }
                if (should_write && client.writeCoilBlock(block.start_address, block.coil_values)) {
                    block.last_written_coil_values = block.coil_values;
                    block.write_initialized = true;
                    block.needs_readback = true;
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        bool should_run_periodic_readback = false;
        if (write_context->periodic_readback_enabled && now >= write_context->next_readback_time) {
            should_run_periodic_readback = true;
            write_context->next_readback_time = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(write_context->readback_interval_seconds)
            );
        }

        const bool has_pending_write_readback = std::any_of(
            write_context->blocks.begin(),
            write_context->blocks.end(),
            [](const WriteMappingBlock& block) {
                return block.needs_readback;
            }
        );
        if (!should_run_periodic_readback && !has_pending_write_readback) {
            return;
        }

        for (auto& block : write_context->blocks) {
            if (!should_run_periodic_readback && !block.needs_readback) {
                continue;
            }

            const int count = static_cast<int>(block.mappings.size());
            if (block.register_type == RegisterType::HoldingRegister) {
                const auto values = client.readHoldingRegisterBlock(block.start_address, count);
                if (values.has_value()) {
                    for (size_t offset = 0; offset < static_cast<size_t>(count); ++offset) {
                        const auto& mapping = block.mappings[offset];
                        drtw->upsertChannelField(
                            mapping.portal,
                            mapping.state_channel,
                            DARTWIC::API::ChannelField::VALUE,
                            static_cast<double>((*values)[offset])
                        );
                    }
                    block.needs_readback = false;
                }
            } else {
                const auto values = client.readCoilBlock(block.start_address, count);
                if (values.has_value()) {
                    for (size_t offset = 0; offset < static_cast<size_t>(count); ++offset) {
                        const auto& mapping = block.mappings[offset];
                        drtw->upsertChannelField(
                            mapping.portal,
                            mapping.state_channel,
                            DARTWIC::API::ChannelField::VALUE,
                            static_cast<double>((*values)[offset])
                        );
                    }
                    block.needs_readback = false;
                }
            }
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


