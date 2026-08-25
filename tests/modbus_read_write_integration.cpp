#include "modbus_tcp_client_module.h"
#include "modbus_tcp_client_plugin.h"

#include <modbus/modbus.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace {
using namespace DARTWIC::API;

double numeric(const ChannelValue& value) {
    return std::visit([](const auto& item) -> double {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, double>) return item;
        if constexpr (std::is_same_v<T, int>) return static_cast<double>(item);
        if constexpr (std::is_same_v<T, bool>) return item ? 1.0 : 0.0;
        return 0.0;
    }, value);
}

class MockRuntime final : public TaskRuntime {
public:
    MockRuntime(std::string name, nlohmann::json arguments)
        : name_(std::move(name)), arguments_(std::move(arguments)) {}
    const std::string& getTaskName() const override { return name_; }
    const std::string& getTaskType() const override { return type_; }
    const nlohmann::json& getMetadata() const override { return metadata_; }
    const nlohmann::json& getArguments() const override { return arguments_; }
    double getElapsedSeconds() const override { return 0.0; }
    bool isStopRequested() const override { return false; }
    void setFixedInputChannels(std::vector<std::string> channels) override {
        fixed_input_channels_ = std::move(channels);
    }
    void setRuntimeContext(const std::string& key, std::shared_ptr<void> value) override {
        contexts_[key] = std::move(value);
    }
    std::shared_ptr<void> getRuntimeContext(const std::string& key) const override {
        const auto found = contexts_.find(key);
        return found == contexts_.end() ? nullptr : found->second;
    }
    void removeRuntimeContext(const std::string& key) override { contexts_.erase(key); }
    void clearRuntimeContext() override { contexts_.clear(); }
private:
    std::string name_;
    std::string type_{"dartwic.modbus-tcp-client.read_write"};
    nlohmann::json metadata_ = nlohmann::json::object();
    nlohmann::json arguments_;
    mutable std::unordered_map<std::string, std::shared_ptr<void>> contexts_;
public:
    std::vector<std::string> fixed_input_channels_;
};

class MockApi final : public SDK_API {
public:
    double queryChannelField(const std::string& channel, ChannelField,
        std::optional<ChannelValue> fallback) override {
        std::scoped_lock lock(mutex_);
        const auto found = values.find(channel);
        return found == values.end() ? (fallback ? numeric(*fallback) : 0.0) : numeric(found->second);
    }
    void insertChannelField(const std::string& channel, ChannelField, ChannelValue value,
        ChannelStorage storage) override {
        std::scoped_lock lock(mutex_);
        if (values.contains(channel)) throw std::runtime_error("duplicate insert");
        values[channel] = std::move(value);
        storages[channel] = storage;
    }
    void upsertChannelField(const std::string& channel, ChannelField field, ChannelValue value,
        ChannelStorage storage) override {
        std::scoped_lock lock(mutex_);
        if (field == ChannelField::VALUE) values[channel] = std::move(value);
        storages[channel] = storage;
    }
    bool removeChannel(const std::string& channel) override {
        std::scoped_lock lock(mutex_);
        storages.erase(channel);
        return values.erase(channel) > 0;
    }
    std::string registerModuleType(ModuleTypeDefinition definition) override {
        module_type = std::move(definition);
        return "dartwic.modbus-tcp-client." + module_type.id;
    }
    std::string registerShareType(ShareTypeDefinition) override { return {}; }
    std::string registerTaskType(std::string local_id, std::string, TaskTypeDefinition definition) override {
        task_type = std::move(definition);
        return "dartwic.modbus-tcp-client." + local_id;
    }
    std::string registerOperation(std::string, std::string, OperationHandler) override { return {}; }
    std::string registerDCodeFunction(std::string, std::string, DCodeFunctionHandler, std::string,
        std::vector<DCodeFunctionArgument>, std::vector<DCodeFunctionArgument>) override { return {}; }
    std::string registerLoop(std::string, std::string, PluginLoopDefinition) override { return {}; }
    int consoleError(std::string, std::string, std::vector<std::string>, std::string, int) override {
        ++error_count;
        return error_count;
    }
    std::shared_ptr<DARTWIC::Modules::BaseModule> getModuleInstance(const std::string& name) override {
        const auto found = modules.find(name);
        return found == modules.end() ? nullptr : found->second;
    }
    std::vector<ModuleInstanceSummary> getModuleInstances(const std::string&) override { return {}; }
    void upsertChannelValueBulk(const std::string&, const std::vector<std::pair<double, uint64_t>>&) override {}
    nlohmann::json callDCodeFunction(const std::string&, const nlohmann::json&) override { return {}; }
    void commandChannel(const std::string& channel, ChannelValue value) override {
        upsertChannelField(channel, ChannelField::VALUE, std::move(value), ChannelStorage::Fixed);
    }
    void setChannel(const std::string& channel, std::optional<ChannelValue> value) override {
        if (value) upsertChannelField(channel, ChannelField::VALUE, std::move(*value), ChannelStorage::Fixed);
    }
    void claimChannel(const std::string&) override {}
    void freeChannel(const std::string&) override {}

    ChannelValue value(const std::string& channel) {
        std::scoped_lock lock(mutex_);
        return values.at(channel);
    }
    ChannelStorage storage(const std::string& channel) {
        std::scoped_lock lock(mutex_);
        return storages.at(channel);
    }

    std::mutex mutex_;
    std::unordered_map<std::string, ChannelValue> values;
    std::unordered_map<std::string, ChannelStorage> storages;
    std::unordered_map<std::string, std::shared_ptr<DARTWIC::Modules::BaseModule>> modules;
    ModuleTypeDefinition module_type;
    TaskTypeDefinition task_type;
    int error_count{0};
};

class Simulator {
public:
    explicit Simulator(int port) : port_(port) {
        context_ = modbus_new_tcp("127.0.0.1", port_);
        mapping_ = modbus_mapping_new(64, 64, 64, 64);
        if (!context_ || !mapping_) throw std::runtime_error("Unable to create Modbus simulator");
        mapping_->tab_input_registers[10] = 321;
        worker_ = std::thread([this]() {
            int socket = modbus_tcp_listen(context_, 1);
            if (socket < 0 || modbus_tcp_accept(context_, &socket) < 0) {
                failed_.store(true);
                return;
            }
            uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
            for (int request = 0; request < 2; ++request) {
                const int length = modbus_receive(context_, query);
                if (length <= 0 || modbus_reply(context_, query, length, mapping_) < 0) {
                    failed_.store(true);
                    return;
                }
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~Simulator() {
        if (worker_.joinable()) worker_.join();
        if (mapping_) modbus_mapping_free(mapping_);
        if (context_) { modbus_close(context_); modbus_free(context_); }
    }
    uint16_t holding(int address) const { return mapping_->tab_registers[address]; }
    bool failed() const { return failed_.load(); }
private:
    int port_;
    modbus_t* context_{};
    modbus_mapping_t* mapping_{};
    std::thread worker_;
    std::atomic<bool> failed_{false};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const int port = 15000 + static_cast<int>(
            std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
        Simulator simulator(port);
        MockApi api;
        ModbusTCPClientPlugin plugin(nlohmann::json::object(), &api);
        plugin.onPluginLoaded();
        auto module = std::shared_ptr<DARTWIC::Modules::BaseModule>(plugin.createModule("tcp_client", {
            {"name", "simulator"},
            {"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", port},
                {"tv_sec", 0}, {"tv_usec", 200000}}}
        }, &api));
        api.modules["simulator"] = module;

        MockRuntime runtime("modbus_cycle", {
            {"module_instance_name", "simulator"},
            {"read_mappings", nlohmann::json::array({
                {{"register", 10}, {"channel", "sensor.pressure"}, {"register_type", "input_register"}}
            })},
            {"write_mappings", nlohmann::json::array({
                {{"register", 20}, {"channel", "command.valve"}, {"register_type", "holding_register"}}
            })}
        });
        api.task_type.on_configure(api.task_type, runtime);
        require(api.storage("sensor.pressure") == ChannelStorage::Fixed &&
            api.storage("command.valve") == ChannelStorage::Fixed,
            "Mapped Modbus channels were not configured Fixed");
        require(runtime.fixed_input_channels_ == std::vector<std::string>{"command.valve"},
            "Modbus configuration did not declare its fixed command snapshot");
        api.upsertChannelField("command.valve", ChannelField::VALUE, 55.0, ChannelStorage::Fixed);
        api.task_type.on_start(api.task_type, runtime);
        api.task_type.on_task(api.task_type, runtime, 0.01);
        api.task_type.on_end(api.task_type, runtime);
        if (simulator.failed()) throw std::runtime_error("Modbus simulator transaction failed");
        require(simulator.holding(20) == 55, "Command snapshot was not written before acquisition");
        require(numeric(api.value("sensor.pressure")) == 321.0, "Sensor input was not published");
        require(numeric(api.value("modbus_cycle.modbus.stale")) == 0.0, "Successful cycle remained stale");

        MockRuntime duplicate("duplicate_cycle", runtime.getArguments());
        bool duplicate_rejected = false;
        try { api.task_type.on_configure(api.task_type, duplicate); }
        catch (const std::exception&) { duplicate_rejected = true; }
        require(duplicate_rejected, "A second task was allowed to own one Modbus connection");
        api.task_type.cleanup(runtime);
        std::cout << "Modbus combined read/write simulator integration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
