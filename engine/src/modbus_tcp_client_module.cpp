#include "modbus_tcp_client_module.h"
#include "modbus_tcp_client_plugin.h"
#include "modbus_register_scanner.h"
#include "modbus_device_discovery.h"

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

enum class RegisterType { InputRegister, DiscreteInput, Coil, HoldingRegister };

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
    bool read_task{false};
};

struct TaskOwnership {
    std::string instance;
    bool read_task{false};
};

std::mutex ownership_mutex;
std::unordered_map<std::string, std::string> module_read_task_owners;
std::unordered_map<std::string, std::string> module_write_task_owners;
std::unordered_map<std::string, TaskOwnership> task_module_ownership;

int validateUnitId(int unit_id) {
    if (unit_id < 0 || (unit_id > 247 && unit_id != 0xFF)) {
        throw std::runtime_error(
            "Modbus Unit ID must be between 0 and 247 (or 255 for the TCP default).");
    }
    return unit_id;
}

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
    if (value == "discrete_input" || value == "discrete_inputs") return RegisterType::DiscreteInput;
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
        if (is_write && (mapping.type == RegisterType::InputRegister || mapping.type == RegisterType::DiscreteInput)) {
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
        if (block.type == RegisterType::Coil || block.type == RegisterType::DiscreteInput) {
            block.coils.resize(block.mappings.size());
        }
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

void configureTask(DARTWIC::API::SDK_API* api, DARTWIC::API::TaskRuntime& runtime, bool read_task) {
    const auto& arguments = runtime.getArguments();
    const std::string instance = arguments.value("module_instance_name", std::string{});
    if (instance.empty()) throw std::runtime_error(
        read_task ? "modbus_tcp_client.read requires module_instance_name." :
            "modbus_tcp_client.write requires module_instance_name.");
    auto module = std::dynamic_pointer_cast<ModbusTCPClientModule>(api->getModuleInstance(instance));
    if (!module || module->dartwic == nullptr) {
        throw std::runtime_error("Configured Modbus module instance `" + instance + "` is not available.");
    }
    auto* module_api = module->dartwic;

    const auto reads = read_task
        ? parseMappings(arguments, "read_mappings", RegisterType::InputRegister)
        : std::vector<Mapping>{};
    const auto writes = read_task
        ? std::vector<Mapping>{}
        : parseMappings(arguments, "write_mappings", RegisterType::Coil, true);
    // Check ownership before channel creation, then verify once more when committing
    // the task-to-module association so a failed reconfiguration keeps its old owner.
    {
        std::scoped_lock lock(ownership_mutex);
        auto& owners = read_task ? module_read_task_owners : module_write_task_owners;
        const auto owner = owners.find(instance);
        if (owner != owners.end() && owner->second != runtime.getTaskName()) {
            throw std::runtime_error("Modbus module `" + instance + "` already has a " +
                (read_task ? "read" : "write") + " task: `" + owner->second + "`.");
        }
    }

    // Use the module-scoped API so RAPID records the producing module as well as
    // the plugin. The plugin-scoped API identifies `plugin:modbus_tcp_client`.
    for (const auto& mapping : reads) createFixedChannel(module_api, mapping.channel);
    for (const auto& mapping : writes) {
        createFixedChannel(module_api, mapping.channel);
        if (!mapping.readback_channel.empty()) createFixedChannel(module_api, mapping.readback_channel);
    }
    std::vector<std::string> fixed_inputs;
    fixed_inputs.reserve(writes.size());
    for (const auto& mapping : writes) fixed_inputs.push_back(mapping.channel);
    runtime.setFixedInputChannels(std::move(fixed_inputs));
    createFixedChannel(module_api, diagnosticChannel(runtime.getTaskName(), "transaction_time_ms"));
    createFixedChannel(module_api, diagnosticChannel(runtime.getTaskName(), "stale"), 1.0);
    createFixedChannel(module_api, diagnosticChannel(runtime.getTaskName(), "failure_count"));
    createFixedChannel(module_api, diagnosticChannel(runtime.getTaskName(), "reconnect_count"));

    {
        std::scoped_lock lock(ownership_mutex);
        auto& owners = read_task ? module_read_task_owners : module_write_task_owners;
        const auto owner = owners.find(instance);
        if (owner != owners.end() && owner->second != runtime.getTaskName()) {
            throw std::runtime_error("Modbus module `" + instance + "` was claimed by task `" + owner->second + "` during configuration.");
        }
        const auto previous = task_module_ownership.find(runtime.getTaskName());
        if (previous != task_module_ownership.end() &&
            (previous->second.instance != instance || previous->second.read_task != read_task)) {
            auto& previous_owners = previous->second.read_task ? module_read_task_owners : module_write_task_owners;
            const auto previous_owner = previous_owners.find(previous->second.instance);
            if (previous_owner != previous_owners.end() && previous_owner->second == runtime.getTaskName()) {
                previous_owners.erase(previous_owner);
            }
        }
        owners[instance] = runtime.getTaskName();
        task_module_ownership[runtime.getTaskName()] = {instance, read_task};
    }
}

std::shared_ptr<RuntimeContext> createRuntime(DARTWIC::API::SDK_API* api,
    DARTWIC::API::TaskRuntime& runtime, bool read_task) {
    const auto& arguments = runtime.getArguments();
    const std::string instance = arguments.value("module_instance_name", std::string{});
    auto module = std::dynamic_pointer_cast<ModbusTCPClientModule>(api->getModuleInstance(instance));
    if (!module) throw std::runtime_error("Configured Modbus module instance `" + instance + "` is not available.");
    auto context = std::make_shared<RuntimeContext>();
    context->module = std::move(module);
    if (read_task) context->read_blocks = buildBlocks(parseMappings(arguments, "read_mappings", RegisterType::InputRegister));
    else context->write_blocks = buildBlocks(parseMappings(arguments, "write_mappings", RegisterType::Coil, true));
    if (read_task ? context->read_blocks.empty() : context->write_blocks.empty()) {
        throw std::runtime_error(read_task
            ? "Configure at least one Modbus read mapping before starting this task."
            : "Configure at least one Modbus write mapping before starting this task.");
    }
    context->readback_interval_seconds = arguments.value("readback_interval_seconds", 0.0);
    context->next_readback = std::chrono::steady_clock::now();
    context->task_name = runtime.getTaskName();
    context->read_task = read_task;
    return context;
}

bool readBlock(ModbusTCPClient& client, MappingBlock& block) {
    switch (block.type) {
        case RegisterType::Coil: return client.readCoils(block.start_address, block.coils);
        case RegisterType::DiscreteInput: return client.readDiscreteInputs(block.start_address, block.coils);
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
        const double value = (block.type == RegisterType::Coil || block.type == RegisterType::DiscreteInput)
            ? static_cast<double>(block.coils[index])
            : static_cast<double>(block.registers[index]);
        api->upsertChannelField(channel, ChannelField::VALUE, value, ChannelStorage::Fixed);
    }
}

void publishDiagnostics(DARTWIC::API::SDK_API* api, const RuntimeContext& context,
    bool succeeded, std::chrono::steady_clock::time_point started) {
    auto& client = context.module->getTCPClient();
    const double transaction_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    api->upsertChannelField(diagnosticChannel(context.task_name, "transaction_time_ms"),
        ChannelField::VALUE, transaction_ms, ChannelStorage::Fixed);
    api->upsertChannelField(diagnosticChannel(context.task_name, "stale"),
        ChannelField::VALUE, succeeded ? 0.0 : 1.0, ChannelStorage::Fixed);
    api->upsertChannelField(diagnosticChannel(context.task_name, "failure_count"),
        ChannelField::VALUE, static_cast<double>(client.failureCount()), ChannelStorage::Fixed);
    api->upsertChannelField(diagnosticChannel(context.task_name, "reconnect_count"),
        ChannelField::VALUE, static_cast<double>(client.reconnectCount()), ChannelStorage::Fixed);
}

void cleanupTaskOwnership(DARTWIC::API::TaskRuntime& runtime) {
    std::scoped_lock lock(ownership_mutex);
    const auto task_owner = task_module_ownership.find(runtime.getTaskName());
    if (task_owner == task_module_ownership.end()) return;
    auto& owners = task_owner->second.read_task ? module_read_task_owners : module_write_task_owners;
    const auto owner = owners.find(task_owner->second.instance);
    if (owner != owners.end() && owner->second == runtime.getTaskName()) owners.erase(owner);
    task_module_ownership.erase(task_owner);
}
} // namespace

ModbusTCPClientModule::ModbusTCPClientModule(nlohmann::json cfg, DARTWIC::API::SDK_API* api)
    : BaseModule(std::move(cfg), api),
      instance_name_(getConfig<std::string>("name")),
      client_(this,
          instance_name_,
          getParameter<std::string>("server_ip"),
          getParameter<int>("server_port", 502),
          validateUnitId(getParameter<int>("unit_id", 0xFF)),
          static_cast<uint32_t>(getParameter<int>("tv_sec", 0)),
          static_cast<uint32_t>(getParameter<int>("tv_usec", 200000)),
          getParameter<std::string>("event_system", "SOFTWARE"),
          getParameter<std::string>("event_subsystem", "MODBUS")) {
    if (getParameter<bool>("connection_monitor_enabled", true)) {
        connection_monitor_thread_ = std::jthread([this](const std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                monitorConnection();
                for (int interval = 0; interval < 10 && !stop_token.stop_requested(); ++interval) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }
}

ModbusTCPClient& ModbusTCPClientModule::getTCPClient() {
    return client_;
}

std::mutex& ModbusTCPClientModule::connectionMutex() {
    return connection_mutex_;
}

void ModbusTCPClientModule::monitorConnection() {
    std::scoped_lock lock(connection_mutex_);
    client_.monitorConnection();
}

void ModbusTCPClientPlugin::onPluginLoaded() {
    dartwic->registerOperation("scan_registers", "Scan Modbus Registers", scanModbusRegisters);

    auto device_finder = createModbusDeviceFinder(dartwic, config);
    dartwic->registerOperation("get_discovery_settings", "Get Modbus Discovery Settings",
        [device_finder](const nlohmann::json&) { return device_finder->settings(); });
    dartwic->registerOperation("configure_discovery", "Configure Modbus Discovery",
        [device_finder](const nlohmann::json& request) {
            return device_finder->configureAddressRange(request);
        });
    dartwic->registerLoop("device_discovery", "Modbus TCP Device Discovery", {
        .on_loop = [device_finder]() { device_finder->tick(); },
        .target_frequency_hz = 1.0,
    });

    dartwic->registerModuleType({
        .id = "tcp_client",
        .name = "Modbus TCP Client"
    });

    DARTWIC::API::TaskTypeDefinition read_task;
    read_task.metadata.structure = DARTWIC::API::TaskStructure::Periodic;
    read_task.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    read_task.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"read_mappings", nlohmann::json::array()},
    };
    read_task.on_configure = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        configureTask(dartwic, runtime, true);
    };
    read_task.on_start = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        runtime.setTypedRuntimeContext("modbus_tcp_client.read", createRuntime(dartwic, runtime, true));
    };
    read_task.on_task = [](const auto&, DARTWIC::API::TaskRuntime& runtime, double) {
        const auto context = runtime.getTypedRuntimeContext<RuntimeContext>("modbus_tcp_client.read");
        if (!context || !context->module || context->module->dartwic == nullptr) return;
        auto* module_api = context->module->dartwic;
        const auto started = std::chrono::steady_clock::now();
        std::scoped_lock connection_lock(context->module->connectionMutex());
        auto& client = context->module->getTCPClient();
        bool succeeded = false;
        if (client.ensureConnected()) {
            succeeded = true;
            for (auto& block : context->read_blocks) {
                if (readBlock(client, block)) {
                    publishBlock(module_api, block, false);
                } else succeeded = false;
            }
        }
        publishDiagnostics(module_api, *context, succeeded, started);
    };
    read_task.cleanup = cleanupTaskOwnership;
    dartwic->registerTaskType("read", "Modbus Read", std::move(read_task));

    DARTWIC::API::TaskTypeDefinition write_task;
    write_task.metadata.structure = DARTWIC::API::TaskStructure::Periodic;
    write_task.metadata.icon_url = "https://upload.wikimedia.org/wikipedia/commons/d/da/Logo_of_Modbus.svg";
    write_task.metadata.default_arguments = {
        {"module_instance_name", ""},
        {"write_mappings", nlohmann::json::array()},
        {"readback_interval_seconds", 0.0}
    };
    write_task.on_configure = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        configureTask(dartwic, runtime, false);
    };
    write_task.on_start = [this](const auto&, DARTWIC::API::TaskRuntime& runtime) {
        runtime.setTypedRuntimeContext("modbus_tcp_client.write", createRuntime(dartwic, runtime, false));
    };
    write_task.on_task = [](const auto&, DARTWIC::API::TaskRuntime& runtime, double) {
        const auto context = runtime.getTypedRuntimeContext<RuntimeContext>("modbus_tcp_client.write");
        if (!context || !context->module || context->module->dartwic == nullptr) return;
        auto* module_api = context->module->dartwic;
        const auto started = std::chrono::steady_clock::now();
        std::scoped_lock connection_lock(context->module->connectionMutex());
        auto& client = context->module->getTCPClient();
        bool succeeded = false;
        if (client.ensureConnected()) {
            succeeded = true;
            for (auto& block : context->write_blocks) {
                for (size_t index = 0; index < block.mappings.size(); ++index) {
                    const double value = module_api->queryChannelField(
                        block.mappings[index].channel, ChannelField::VALUE, DARTWIC::API::ChannelValue{0.0});
                    if (block.type == RegisterType::Coil) block.coils[index] = value != 0.0 ? 1 : 0;
                    else block.registers[index] = toRegister(value);
                }
                const bool wrote = block.type == RegisterType::Coil
                    ? client.writeCoils(block.start_address, block.coils)
                    : client.writeHoldingRegisters(block.start_address, block.registers);
                if (!wrote) succeeded = false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (context->readback_interval_seconds > 0.0 && now >= context->next_readback) {
                context->next_readback = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(context->readback_interval_seconds));
                for (auto& block : context->write_blocks) {
                    if (readBlock(client, block)) publishBlock(module_api, block, true);
                    else succeeded = false;
                }
            }
        }
        publishDiagnostics(module_api, *context, succeeded, started);
    };
    write_task.cleanup = cleanupTaskOwnership;
    dartwic->registerTaskType("write", "Modbus Write", std::move(write_task));
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
