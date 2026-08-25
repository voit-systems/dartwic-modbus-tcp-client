#include "modbus_tcp_client_module.h"
#include "modbus_tcp_client_plugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
using DARTWIC::API::ChannelField;
using DARTWIC::API::ChannelStorage;

enum class RegisterType { InputRegister, Coil, HoldingRegister };

struct Mapping {
    int address{0};
    std::string channel;
    std::string readback_channel;
    RegisterType type{RegisterType::InputRegister};
};

struct MappingBlock {
    int start_address{0};
    RegisterType type{RegisterType::InputRegister};
    std::vector<Mapping> mappings;
    std::vector<uint16_t> registers;
    std::vector<uint8_t> coils;
};

struct RuntimeContext {
    std::shared_ptr<ModbusTCPClientModule> module;
    std::vector<MappingBlock> read_blocks;
    std::vector<MappingBlock> write_blocks;
    double readback_interval_seconds{0.0};
    std::chrono::steady_clock::time_point next_readback{};
    std::string task_name;
};

std::mutex ownership_mutex;
std::unordered_map<std::string, std::string> module_task_owners;
std::unordered_map<std::string, std::string> task_module_ownership;

std::string normalizedChannel(std::string channel) {
    if (!channel.empty() && channel.front() == '|' && channel.back() == '|') {
        channel = channel.substr(1, channel.size() - 2);
    }
    std::replace(channel.begin(), channel.end(), '/', '.');
    return channel;
}

std::optional<RegisterType> parseType(const nlohmann::json& item, RegisterType fallback) {
    const std::string value = item.value("register_type", std::string{});
    if (value.empty()) return fallback;
    if (value == "coil" || value == "coils") return RegisterType::Coil;
    if (value == "holding_register" || value == "holding_registers") return RegisterType::HoldingRegister;
    if (value == "input_register" || value == "input_registers") return RegisterType::InputRegister;
    return std::nullopt;
}

std::vector<Mapping> parseMappings(const nlohmann::json& arguments,
    const char* key,
    RegisterType fallback,
    bool is_write = false) {
    std::vector<Mapping> result;
    if (!arguments.is_object() || !arguments.contains(key) || !arguments[key].is_array()) return result;
    size_t mapping_index = 0;
    for (const auto& item : arguments[key]) {
        if (!item.is_object() || !item.contains("register") || !item["register"].is_number_integer() ||
            !item.contains("channel") || !item["channel"].is_string()) {
            throw std::runtime_error(std::string(key) + "[" + std::to_string(mapping_index) +
                "] requires an integer register and a channel string.");
        }
        Mapping mapping;
        mapping.address = item["register"].get<int>();
        mapping.channel = normalizedChannel(item["channel"].get<std::string>());
        mapping.readback_channel = normalizedChannel(item.value("readback_channel", std::string{}));
        const auto parsed_type = parseType(item, fallback);
        if (!parsed_type) {
            throw std::runtime_error(std::string(key) + "[" + std::to_string(mapping_index) +
                "] has an unsupported register_type.");
        }
        mapping.type = *parsed_type;
        if (is_write && mapping.type == RegisterType::InputRegister) {
            throw std::runtime_error(std::string(key) + "[" + std::to_string(mapping_index) +
                "] cannot write an input register.");
        }
        if (mapping.address < 0 || mapping.channel.empty()) {
            throw std::runtime_error(std::string(key) + "[" + std::to_string(mapping_index) +
                "] requires a non-negative register and non-empty channel.");
        }
        result.push_back(std::move(mapping));
        ++mapping_index;
    }
    std::sort(result.begin(), result.end(), [](const Mapping& left, const Mapping& right) {
        if (left.type != right.type) return static_cast<int>(left.type) < static_cast<int>(right.type);
        return left.address < right.address;
    });
    return result;
}

std::vector<MappingBlock> buildBlocks(const std::vector<Mapping>& mappings) {
    std::vector<MappingBlock> blocks;
    size_t index = 0;
    while (index < mappings.size()) {
        size_t end = index + 1;
        while (end < mappings.size() && mappings[end].type == mappings[index].type &&
            mappings[end].address == mappings[end - 1].address + 1) ++end;
        MappingBlock block;
        block.start_address = mappings[index].address;
        block.type = mappings[index].type;
        block.mappings.assign(mappings.begin() + static_cast<std::ptrdiff_t>(index),
            mappings.begin() + static_cast<std::ptrdiff_t>(end));
        if (block.type == RegisterType::Coil) block.coils.resize(block.mappings.size());
        else block.registers.resize(block.mappings.size());
        blocks.push_back(std::move(block));
        index = end;
    }
    return blocks;
}

uint16_t toRegister(double value) {
    if (value <= 0.0) return 0;
    if (value >= static_cast<double>((std::numeric_limits<uint16_t>::max)())) {
        return (std::numeric_limits<uint16_t>::max)();
    }
    return static_cast<uint16_t>(std::llround(value));
}

std::string diagnosticChannel(const std::string& task_name, const char* suffix) {
    return task_name + ".modbus." + suffix;
}

void createFixedChannel(DARTWIC::API::SDK_API* api, const std::string& channel, double initial_value = 0.0) {
    api->upsertChannelField(channel, ChannelField::VALUE, initial_value, ChannelStorage::Fixed);
}

void configureTask(DARTWIC::API::SDK_API* api, DARTWIC::API::TaskRuntime& runtime) {
    const auto& arguments = runtime.getArguments();
    const std::string instance = arguments.value("module_instance_name", std::string{});
    if (instance.empty()) throw std::runtime_error("modbus.read_write requires module_instance_name.");

    const auto reads = parseMappings(arguments, "read_mappings", RegisterType::InputRegister);
    const auto writes = parseMappings(arguments, "write_mappings", RegisterType::Coil, true);
    if (reads.empty() && writes.empty()) {
        throw std::runtime_error("modbus.read_write requires at least one read or write mapping.");
    }

    // Check ownership before channel creation, then verify once more when committing
    // the task-to-module association so a failed reconfiguration keeps its old owner.
    {
        std::scoped_lock lock(ownership_mutex);
        const auto owner = module_task_owners.find(instance);
        if (owner != module_task_owners.end() && owner->second != runtime.getTaskName()) {
            throw std::runtime_error("Modbus module `" + instance + "` is already owned by task `" + owner->second + "`.");
        }
    }

    for (const auto& mapping : reads) createFixedChannel(api, mapping.channel);
    for (const auto& mapping : writes) {
        createFixedChannel(api, mapping.channel);
        if (!mapping.readback_channel.empty()) createFixedChannel(api, mapping.readback_channel);
    }
    createFixedChannel(api, diagnosticChannel(runtime.getTaskName(), "transaction_time_ms"));
    createFixedChannel(api, diagnosticChannel(runtime.getTaskName(), "stale"), 1.0);
    createFixedChannel(api, diagnosticChannel(runtime.getTaskName(), "failure_count"));
    createFixedChannel(api, diagnosticChannel(runtime.getTaskName(), "reconnect_count"));

    {
        std::scoped_lock lock(ownership_mutex);
        const auto owner = module_task_owners.find(instance);
        if (owner != module_task_owners.end() && owner->second != runtime.getTaskName()) {
            throw std::runtime_error("Modbus module `" + instance + "` was claimed by task `" + owner->second + "` during configuration.");
        }
        const auto previous = task_module_ownership.find(runtime.getTaskName());
        if (previous != task_module_ownership.end() && previous->second != instance) {
            const auto previous_owner = module_task_owners.find(previous->second);
            if (previous_owner != module_task_owners.end() && previous_owner->second == runtime.getTaskName()) {
                module_task_owners.erase(previous_owner);
            }
        }
        module_task_owners[instance] = runtime.getTaskName();
        task_module_ownership[runtime.getTaskName()] = instance;
    }
}

std::shared_ptr<RuntimeContext> createRuntime(DARTWIC::API::SDK_API* api,
    DARTWIC::API::TaskRuntime& runtime) {
    const auto& arguments = runtime.getArguments();
    const std::string instance = arguments.value("module_instance_name", std::string{});
    auto module = std::dynamic_pointer_cast<ModbusTCPClientModule>(api->getModuleInstance(instance));
    if (!module) throw std::runtime_error("Configured Modbus module instance `" + instance + "` is not available.");
    auto context = std::make_shared<RuntimeContext>();
    context->module = std::move(module);
    context->read_blocks = buildBlocks(parseMappings(arguments, "read_mappings", RegisterType::InputRegister));
    context->write_blocks = buildBlocks(parseMappings(arguments, "write_mappings", RegisterType::Coil, true));
    context->readback_interval_seconds = arguments.value("readback_interval_seconds", 0.0);
    context->next_readback = std::chrono::steady_clock::now();
    context->task_name = runtime.getTaskName();
    return context;
}

bool readBlock(ModbusTCPClient& client, MappingBlock& block) {
    switch (block.type) {
        case RegisterType::Coil: return client.readCoils(block.start_address, block.coils);
        case RegisterType::HoldingRegister: return client.readHoldingRegisters(block.start_address, block.registers);
        case RegisterType::InputRegister: return client.readInputRegisters(block.start_address, block.registers);
    }
    return false;
}

void publishBlock(DARTWIC::API::SDK_API* api, const MappingBlock& block, bool use_readback_names) {
    for (size_t index = 0; index < block.mappings.size(); ++index) {
        const auto& mapping = block.mappings[index];
        const std::string& channel = use_readback_names ? mapping.readback_channel : mapping.channel;
        if (channel.empty()) continue;
        const double value = block.type == RegisterType::Coil
            ? static_cast<double>(block.coils[index])
            : static_cast<double>(block.registers[index]);
        api->upsertChannelField(channel, ChannelField::VALUE, value, ChannelStorage::Fixed);
    }
}
} // namespace

ModbusTCPClientModule::ModbusTCPClientModule(nlohmann::json cfg, DARTWIC::API::SDK_API* api)
    : BaseModule(std::move(cfg), api),
      instance_name_(getConfig<std::string>("name")),
      client_(this,
          instance_name_,
          getParameter<std::string>("server_ip"),
          getParameter<int>("server_port", 502),
          static_cast<uint32_t>(getParameter<int>("tv_sec", 0)),
          static_cast<uint32_t>(getParameter<int>("tv_usec", 200000))) {}

ModbusTCPClient& ModbusTCPClientModule::getTCPClient() {
    return client_;
}

void ModbusTCPClientPlugin::onPluginLoaded() {
    dartwic->registerModuleType({
        .id = "tcp_client",
        .name = "Modbus TCP Client"
    });

    DARTWIC::API::TaskTypeDefinition task;
    task.metadata.structure = DARTWIC::API::TaskStructure::Periodic;
    task.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    task.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"read_mappings", nlohmann::json::array()},
        {"write_mappings", nlohmann::json::array()},
        {"readback_interval_seconds", 0.0}
    };
    task.on_configure = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        configureTask(dartwic, runtime);
    };
    task.on_start = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        runtime.setTypedRuntimeContext("modbus.read_write", createRuntime(dartwic, runtime));
    };
    task.on_task = [this](const auto&, DARTWIC::API::TaskRuntime& runtime, double) {
        const auto context = runtime.getTypedRuntimeContext<RuntimeContext>("modbus.read_write");
        if (!context || !context->module) return;
        const auto started = std::chrono::steady_clock::now();
        auto& client = context->module->getTCPClient();
        bool any_read_succeeded = context->read_blocks.empty();

        // Cycle N first writes the complete command snapshot pinned when CAESAR entered this callback.
        if (client.ensureConnected()) {
            for (auto& block : context->write_blocks) {
                for (size_t index = 0; index < block.mappings.size(); ++index) {
                    const double value = dartwic->queryChannelField(
                        block.mappings[index].channel, ChannelField::VALUE, DARTWIC::API::ChannelValue{0.0});
                    if (block.type == RegisterType::Coil) block.coils[index] = value != 0.0 ? 1 : 0;
                    else block.registers[index] = toRegister(value);
                }
                if (block.type == RegisterType::Coil) client.writeCoils(block.start_address, block.coils);
                else client.writeHoldingRegisters(block.start_address, block.registers);
            }

            // Then acquire and stage every successful sensor block for one fixed RAPID commit.
            for (auto& block : context->read_blocks) {
                if (readBlock(client, block)) {
                    publishBlock(dartwic, block, false);
                    any_read_succeeded = true;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (context->readback_interval_seconds > 0.0 && now >= context->next_readback) {
                context->next_readback = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(context->readback_interval_seconds));
                for (auto& block : context->write_blocks) {
                    if (readBlock(client, block)) publishBlock(dartwic, block, true);
                }
            }
        }

        const double transaction_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        dartwic->upsertChannelField(diagnosticChannel(context->task_name, "transaction_time_ms"),
            ChannelField::VALUE, transaction_ms, ChannelStorage::Fixed);
        dartwic->upsertChannelField(diagnosticChannel(context->task_name, "stale"),
            ChannelField::VALUE, any_read_succeeded ? 0.0 : 1.0, ChannelStorage::Fixed);
        dartwic->upsertChannelField(diagnosticChannel(context->task_name, "failure_count"),
            ChannelField::VALUE, static_cast<double>(client.failureCount()), ChannelStorage::Fixed);
        dartwic->upsertChannelField(diagnosticChannel(context->task_name, "reconnect_count"),
            ChannelField::VALUE, static_cast<double>(client.reconnectCount()), ChannelStorage::Fixed);
    };
    task.on_end = [](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        const auto context = runtime.getTypedRuntimeContext<RuntimeContext>("modbus.read_write");
        if (context && context->module) context->module->getTCPClient().disconnect();
    };
    task.cleanup = [](DARTWIC::API::TaskRuntime& runtime) {
        std::scoped_lock lock(ownership_mutex);
        const auto task_owner = task_module_ownership.find(runtime.getTaskName());
        const std::string instance = task_owner != task_module_ownership.end()
            ? task_owner->second
            : runtime.getArguments().value("module_instance_name", std::string{});
        const auto owner = module_task_owners.find(instance);
        if (owner != module_task_owners.end() && owner->second == runtime.getTaskName()) module_task_owners.erase(owner);
        if (task_owner != task_module_ownership.end()) task_module_ownership.erase(task_owner);
    };
    dartwic->registerTaskType("read_write", "Modbus Read / Write", std::move(task));
}

DARTWIC::Modules::BaseModule* ModbusTCPClientPlugin::createModule(
    const std::string& module_type_id,
    nlohmann::json cfg,
    DARTWIC::API::SDK_API* api) {
    if (module_type_id != "tcp_client") return nullptr;
    return new ModbusTCPClientModule(std::move(cfg), api);
}

DARTWIC_PLUGIN_EXPORT DARTWIC::Plugins::BasePlugin* createPlugin(
    nlohmann::json cfg,
    DARTWIC::API::SDK_API* api) {
    return new ModbusTCPClientPlugin(std::move(cfg), api);
}
