#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include "modbus_tcp_client_module.h"
#include "modbus_tcp_client_plugin.h"
#include "modbus_device_discovery.h"

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
    MockRuntime(std::string name, std::string type, nlohmann::json arguments)
        : name_(std::move(name)), type_(std::move(type)), arguments_(std::move(arguments)) {}
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
    std::string type_;
    nlohmann::json metadata_ = nlohmann::json::object();
    nlohmann::json arguments_;
    mutable std::unordered_map<std::string, std::shared_ptr<void>> contexts_;
public:
    std::vector<std::string> fixed_input_channels_;
};

class MockApi final : public SDK_API {
public:
    bool discovery_muted{false};
    bool mute_lookup_failed{false};
    int discovery_requests{0};
    nlohmann::json discovery_request;
    bool isNotificationMuted(const std::string& id) override {
        if (id != "device-discovery:127.0.0.1:15099") throw std::runtime_error("Unexpected notification identity");
        if (mute_lookup_failed) throw std::runtime_error("Preference store unavailable");
        return discovery_muted;
    }
    nlohmann::json requestInterfaceUi(const std::string& ui, nlohmann::json payload, nlohmann::json options) override {
        ++discovery_requests;
        discovery_request = {{"request_id", "discovery-test"}, {"ui_id", ui},
            {"payload", std::move(payload)}, {"options", std::move(options)}, {"status", "pending"}, {"muted", false}};
        return discovery_request;
    }
    nlohmann::json getInterfaceUiRequest(const std::string&) override { return discovery_request; }
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
    std::string registerShareTransport(ShareTransportDefinition) override { return {}; }
    std::string registerTaskType(std::string local_id, std::string, TaskTypeDefinition definition) override {
        task_types[local_id] = std::move(definition);
        return "dartwic.modbus-tcp-client." + local_id;
    }
    std::string registerOperation(std::string, std::string, OperationHandler) override { return {}; }
    std::string registerDCodeFunction(std::string, std::string, DCodeFunctionHandler, std::string,
        std::vector<DCodeFunctionArgument>, std::vector<DCodeFunctionArgument>) override { return {}; }
    std::string registerLoop(std::string local_id, std::string, PluginLoopDefinition definition) override {
        loops[local_id] = std::move(definition);
        return "modbus_tcp_client." + local_id;
    }
    int consoleError(std::string title, std::string description, std::vector<std::string>, std::string, int) override {
        std::scoped_lock lock(mutex_);
        last_error_title = std::move(title);
        last_error_description = std::move(description);
        ++error_count;
        return error_count;
    }
    nlohmann::json recordEvent(nlohmann::json event) override {
        std::scoped_lock lock(mutex_);
        ++argus_event_count;
        last_argus_event = std::move(event);
        last_argus_event["event_id"] = "event_" + last_argus_event.value("correlation_key", std::string{"modbus"});
        active_event_id = last_argus_event["event_id"].get<std::string>();
        return last_argus_event;
    }
    bool updateEventStatus(const std::string& event_id, const std::string& status) override {
        std::scoped_lock lock(mutex_);
        if (event_id.empty()) return false;
        updated_event_id = event_id;
        updated_event_status = status;
        if (event_id == active_event_id && status == "resolved") active_event_id.clear();
        return true;
    }
    std::shared_ptr<DARTWIC::Modules::BaseModule> getModuleInstance(const std::string& name) override {
        const auto found = modules.find(name);
        return found == modules.end() ? nullptr : found->second;
    }
    std::vector<ModuleInstanceSummary> getModuleInstances(const std::string&) override { return module_summaries; }
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
    FixedChannelBatch resolveFixedChannels(const std::vector<std::string>& channels) override {
        return {channels, std::vector<FixedChannelHandle>(channels.size())};
    }
    void queryFixedChannelValues(const FixedChannelBatch&, std::span<double>, double) override {}
    void upsertFixedChannelValues(const FixedChannelBatch&, std::span<const double>,
        std::optional<uint64_t>) override {}

    ChannelValue value(const std::string& channel) {
        std::scoped_lock lock(mutex_);
        return values.at(channel);
    }
    ChannelStorage storage(const std::string& channel) {
        std::scoped_lock lock(mutex_);
        return storages.at(channel);
    }
    int eventCount() {
        std::scoped_lock lock(mutex_);
        return argus_event_count;
    }
    nlohmann::json lastEvent() {
        std::scoped_lock lock(mutex_);
        return last_argus_event;
    }

    std::mutex mutex_;
    std::unordered_map<std::string, ChannelValue> values;
    std::unordered_map<std::string, ChannelStorage> storages;
    std::unordered_map<std::string, std::shared_ptr<DARTWIC::Modules::BaseModule>> modules;
    ModuleTypeDefinition module_type;
    std::unordered_map<std::string, TaskTypeDefinition> task_types;
    std::unordered_map<std::string, PluginLoopDefinition> loops;
    std::vector<ModuleInstanceSummary> module_summaries;
    int error_count{0};
    std::string last_error_title;
    std::string last_error_description;
    int argus_event_count{0};
    nlohmann::json last_argus_event = nlohmann::json::object();
    std::string active_event_id;
    std::string updated_event_id;
    std::string updated_event_status;
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

class TimeoutRecoverySimulator {
public:
    explicit TimeoutRecoverySimulator(int port) : port_(port) {
        context_ = modbus_new_tcp("127.0.0.1", port_);
        mapping_ = modbus_mapping_new(8, 8, 8, 8);
        if (!context_ || !mapping_) throw std::runtime_error("Unable to create timeout-recovery simulator");
        mapping_->tab_input_registers[0] = 777;
        worker_ = std::thread([this]() {
            int server_socket = modbus_tcp_listen(context_, 3);
            if (server_socket < 0) {
                failed_.store(true);
                return;
            }
            uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
            for (int connection = 0; connection < 3; ++connection) {
                if (modbus_tcp_accept(context_, &server_socket) < 0) {
                    failed_.store(true);
                    return;
                }
                const int length = modbus_receive(context_, query);
                if (length <= 0) {
                    failed_.store(true);
                    return;
                }
                // Deliberately drop two responses. A timed-out synchronous
                // Modbus TCP stream is unsafe to reuse because the late reply
                // could be mistaken for the next transaction.
                if (connection < 2) {
                    modbus_close(context_);
                } else if (modbus_reply(context_, query, length, mapping_) < 0) {
                    failed_.store(true);
                    return;
                }
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~TimeoutRecoverySimulator() {
        if (worker_.joinable()) worker_.join();
        if (mapping_) modbus_mapping_free(mapping_);
        if (context_) { modbus_close(context_); modbus_free(context_); }
    }
    bool failed() const { return failed_.load(); }
private:
    int port_{};
    modbus_t* context_{};
    modbus_mapping_t* mapping_{};
    std::thread worker_;
    std::atomic<bool> failed_{false};
};

class AbruptDisconnectSimulator {
public:
    explicit AbruptDisconnectSimulator(int port) {
        context_ = modbus_new_tcp("127.0.0.1", port);
        mapping_ = modbus_mapping_new(8, 8, 8, 8);
        if (!context_ || !mapping_) throw std::runtime_error("Unable to create disconnect simulator");
        mapping_->tab_input_registers[0] = 909;
        worker_ = std::thread([this]() {
            int server_socket = modbus_tcp_listen(context_, 1);
            if (server_socket < 0 || modbus_tcp_accept(context_, &server_socket) < 0) {
                failed_.store(true);
                return;
            }
            uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
            const int length = modbus_receive(context_, query);
            if (length <= 0 || modbus_reply(context_, query, length, mapping_) < 0) {
                failed_.store(true);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const int client_socket = modbus_get_socket(context_);
            linger reset_linger{};
            reset_linger.l_onoff = 1;
            reset_linger.l_linger = 0;
            if (setsockopt(client_socket, SOL_SOCKET, SO_LINGER,
                    reinterpret_cast<const char*>(&reset_linger), sizeof(reset_linger)) != 0) {
                failed_.store(true);
            }
            modbus_close(context_);
            dropped_.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ~AbruptDisconnectSimulator() {
        if (worker_.joinable()) worker_.join();
        if (mapping_) modbus_mapping_free(mapping_);
        if (context_) modbus_free(context_);
    }
    bool waitForDrop() const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!dropped_.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return dropped_.load(std::memory_order_acquire);
    }
    bool failed() const { return failed_.load(); }
private:
    modbus_t* context_{};
    modbus_mapping_t* mapping_{};
    std::thread worker_;
    std::atomic<bool> dropped_{false};
    std::atomic<bool> failed_{false};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testDiscoveryLifecycle() {
    MockApi api;
    int scans = 0;
    bool mute_during_scan = false;
    const nlohmann::json config = {{"device_discovery", {
        {"network_scan", {{"enabled", false}}},
        {"targets", nlohmann::json::array({{{"host", "127.0.0.1"}, {"port", 15099}, {"unit_ids", {1}},
            {"start_address", 0}, {"end_address", 1}}})}
    }}};
    ModbusDeviceFinder finder(&api, config, [&](const nlohmann::json&) {
        ++scans;
        if (mute_during_scan) api.discovery_muted = true;
        return nlohmann::json{{"scanned", {{"start_address", 0}, {"end_address", 1}}}};
    });
    api.discovery_muted = true;
    finder.tick();
    finder.tick();
    require(scans == 0 && api.discovery_requests == 0, "Muted discovery must not scan or announce");
    api.discovery_muted = false;
    finder.tick();
    require(scans == 1 && api.discovery_requests == 1, "Unmute must offer the device exactly once");
    const auto& options = api.discovery_request["options"];
    require(options["silenceable"] == true && options["mute_scope"] == "engine",
        "Discovery must expose engine-scoped mute");
    require(options["notification_id"] == "modbus_tcp_client:device-discovery:127.0.0.1:15099",
        "Discovery and mute lookup identities must match");
    finder.tick();
    require(scans == 1 && api.discovery_requests == 1, "Pending discovery must not repeat");
    api.discovery_muted = true;
    finder.tick();
    finder.tick();
    require(scans == 1 && api.discovery_requests == 1, "Mute callback must suppress subsequent work");
    api.discovery_muted = false;
    finder.tick();
    require(scans == 2 && api.discovery_requests == 2, "Unmute callback must permit one fresh offer");
    api.discovery_request["status"] = "completed";
    api.modules["renamed"] = std::make_shared<DARTWIC::Modules::BaseModule>(
        nlohmann::json{{"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", 15099}, {"unit_id", 1}}}}, &api);
    api.module_summaries.push_back({"renamed", "modbus_tcp_client", "tcp_client", ""});
    finder.tick();
    require(scans == 2 && api.discovery_requests == 2, "Configured device must not be offered again");
    api.modules.clear();
    api.module_summaries.clear();
    mute_during_scan = true;
    finder.tick();
    require(scans == 3 && api.discovery_requests == 2, "Mute during scan must prevent announcement");
    api.discovery_muted = false;
    api.mute_lookup_failed = true;
    finder.tick();
    require(scans == 3 && api.discovery_requests == 2, "Unknown mute state must defer discovery");
    api.mute_lookup_failed = false;
    mute_during_scan = false;
    finder.tick();
    require(scans == 4 && api.discovery_requests == 3, "Removed device can be rediscovered after unmute");
    std::cout << "Discovery mute/configuration lifecycle passed." << std::endl;
}
} // namespace

int main() {
    try {
        testDiscoveryLifecycle();
        const int port = 15000 + static_cast<int>(
            std::chrono::steady_clock::now().time_since_epoch().count() % 1000);
        Simulator simulator(port);
        MockApi api;
        ModbusTCPClientPlugin plugin(nlohmann::json::object(), &api);
        plugin.onPluginLoaded();
        auto module = std::shared_ptr<DARTWIC::Modules::BaseModule>(plugin.createModule("tcp_client", {
            {"name", "simulator"},
            {"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", port},
                {"tv_sec", 0}, {"tv_usec", 200000}, {"connection_monitor_enabled", false}}}
        }, &api));
        api.modules["simulator"] = module;

        MockRuntime read_runtime("modbus_read", "modbus_tcp_client.read", {
            {"module_instance_name", "simulator"},
            {"read_mappings", nlohmann::json::array({
                {{"register", 10}, {"channel", "sensor.pressure"}, {"register_type", "input_register"}}
            })}
        });
        MockRuntime write_runtime("modbus_write", "modbus_tcp_client.write", {
            {"module_instance_name", "simulator"},
            {"write_mappings", nlohmann::json::array({
                {{"register", 20}, {"channel", "command.valve"}, {"register_type", "holding_register"}}
            })}
        });
        auto& read_type = api.task_types.at("read");
        auto& write_type = api.task_types.at("write");
        read_type.on_configure(read_type, read_runtime);
        write_type.on_configure(write_type, write_runtime);
        require(api.storage("sensor.pressure") == ChannelStorage::Fixed &&
            api.storage("command.valve") == ChannelStorage::Fixed,
            "Mapped Modbus channels were not configured Fixed");
        require(write_runtime.fixed_input_channels_ == std::vector<std::string>{"command.valve"},
            "Modbus configuration did not declare its fixed command snapshot");
        api.upsertChannelField("command.valve", ChannelField::VALUE, 55.0, ChannelStorage::Fixed);
        read_type.on_start(read_type, read_runtime);
        read_type.on_task(read_type, read_runtime, 0.01);
        write_type.on_start(write_type, write_runtime);
        write_type.on_task(write_type, write_runtime, 0.01);
        if (simulator.failed()) throw std::runtime_error("Modbus simulator transaction failed");
        require(simulator.holding(20) == 55, "Command snapshot was not written");
        require(numeric(api.value("sensor.pressure")) == 321.0, "Sensor input was not published");
        require(numeric(api.value("modbus_read.modbus.stale")) == 0.0, "Successful read remained stale");

        const int timeout_recovery_port = port + 1000;
        TimeoutRecoverySimulator timeout_recovery_simulator(timeout_recovery_port);
        auto recovery_module = std::shared_ptr<DARTWIC::Modules::BaseModule>(plugin.createModule("tcp_client", {
            {"name", "recovery"},
            {"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", timeout_recovery_port},
                {"unit_id", 1}, {"tv_sec", 0}, {"tv_usec", 50000},
                {"connection_monitor_enabled", false}}}
        }, &api));
        auto typed_recovery_module = std::dynamic_pointer_cast<ModbusTCPClientModule>(recovery_module);
        require(static_cast<bool>(typed_recovery_module), "Timeout-recovery module was not created");
        std::vector<uint16_t> recovery_values(1, 0);
        auto& recovery_client = typed_recovery_module->getTCPClient();
        const bool first_recovery_read = recovery_client.readInputRegisters(0, recovery_values);
        require(!recovery_client.isConnected(),
            "Timed-out Modbus connection was retained with an ambiguous response stream");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const bool second_recovery_read = recovery_client.readInputRegisters(0, recovery_values);
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        const bool third_recovery_read = recovery_client.readInputRegisters(0, recovery_values);
        require(!first_recovery_read,
            "First deliberately dropped response unexpectedly succeeded");
        require(!second_recovery_read,
            "Second deliberately dropped response unexpectedly succeeded");
        require(recovery_client.reconnectCount() == 3,
            "Modbus timeout recovery did not use a fresh connection per ambiguous stream");
        require(third_recovery_read && recovery_values[0] == 777,
            "Modbus client did not recover after bounded reconnect backoff");
        require(!timeout_recovery_simulator.failed(), "Timeout-recovery simulator failed");
        require(numeric(api.value("recovery.info.connected")) == 1.0,
            "Protocol recovery did not publish connected health");

        const int disconnect_port = port + 2000;
        AbruptDisconnectSimulator disconnect_simulator(disconnect_port);
        auto disconnect_module = std::shared_ptr<DARTWIC::Modules::BaseModule>(plugin.createModule("tcp_client", {
            {"name", "disconnect"},
            {"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", disconnect_port},
                {"unit_id", 1}, {"tv_sec", 0}, {"tv_usec", 200000},
                {"connection_monitor_enabled", false}}}
        }, &api));
        auto typed_disconnect_module = std::dynamic_pointer_cast<ModbusTCPClientModule>(disconnect_module);
        require(static_cast<bool>(typed_disconnect_module), "Disconnect-test module was not created");
        auto& disconnect_client = typed_disconnect_module->getTCPClient();
        std::vector<uint16_t> disconnect_values(1, 0);
        require(disconnect_client.readInputRegisters(0, disconnect_values) && disconnect_values[0] == 909,
            "Disconnect-test client did not establish initial protocol health");
        require(numeric(api.value("disconnect.info.connected")) == 1.0,
            "Initial successful transaction did not publish connected health");
        require(disconnect_simulator.waitForDrop(), "Disconnect-test server did not close its client socket");
        const int events_before_disconnect = api.argus_event_count;
        require(!disconnect_client.readInputRegisters(0, disconnect_values),
            "Transaction unexpectedly succeeded after the server closed its socket");
        require(!disconnect_client.isConnected() && numeric(api.value("disconnect.info.connected")) == 0.0,
            "Server disconnect did not immediately clear client and RAPID connection state");
        require(api.argus_event_count > events_before_disconnect &&
            api.last_argus_event.value("title", std::string{}).find("MODBUS CONNECTION ERROR") != std::string::npos,
            "Server disconnect was not classified as a dedicated ARGUS connection event");
        require(api.last_argus_event.value("description", std::string{}).find("No error") == std::string::npos,
            "Server disconnect produced the misleading no-error message");
        require(!disconnect_simulator.failed(), "Disconnect simulator failed");

        const int idle_port = port + 3000;
        const int events_before_idle_monitor = api.eventCount();
        auto idle_module = std::shared_ptr<DARTWIC::Modules::BaseModule>(plugin.createModule("tcp_client", {
            {"name", "idle"},
            {"parameters", {{"server_ip", "127.0.0.1"}, {"server_port", idle_port},
                {"unit_id", 1}, {"tv_sec", 0}, {"tv_usec", 50000},
                {"event_system", "GROUND"}, {"event_subsystem", "PLC"}}}
        }, &api));
        api.modules["idle"] = idle_module;
        for (int attempt = 0; attempt < 20 && api.eventCount() == events_before_idle_monitor; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        const auto idle_event = api.lastEvent();
        require(numeric(api.value("idle.info.connected")) == 0.0,
            "Idle disconnected module did not retain disconnected channel state");
        require(api.eventCount() > events_before_idle_monitor,
            "Connection monitor did not trigger an event while Modbus tasks were stopped");
        require(idle_event.value("system", std::string{}) == "GROUND" &&
            idle_event.value("subsystem", std::string{}) == "PLC",
            "Connection event did not use the configured ARGUS system and subsystem");
        require(idle_event.value("auto_acknowledge_seconds", -1) == 0,
            "Connection event unexpectedly auto-acknowledges while the fault is firing");

        MockRuntime duplicate("duplicate_read", "modbus_tcp_client.read", read_runtime.getArguments());
        bool duplicate_rejected = false;
        try { read_type.on_configure(read_type, duplicate); }
        catch (const std::exception&) { duplicate_rejected = true; }
        require(duplicate_rejected, "A second read task was allowed on one Modbus connection");
        read_type.cleanup(read_runtime);
        write_type.cleanup(write_runtime);
        std::cout << "Modbus split read/write simulator integration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
