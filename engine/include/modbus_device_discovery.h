#pragma once

#include <sdk/sdk_api.h>

#include <cstddef>
#include <chrono>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class ModbusDeviceFinder {
public:
    ModbusDeviceFinder(DARTWIC::API::SDK_API* api, const nlohmann::json& plugin_config);
    void tick();
    nlohmann::json settings() const;
    nlohmann::json configureAddressRange(const nlohmann::json& request);

private:
    struct Target {
        std::string host{"127.0.0.1"};
        int port{502};
        std::vector<int> unit_ids{1, 255};
        int start_address{0};
        int end_address{255};
        int timeout_ms{100};
        bool scan_all_unit_ids{false};
        bool auto_map_responding_addresses{true};
        bool network_discovered{false};
    };

    struct NetworkScan {
        bool enabled{true};
        bool include_local_subnets{true};
        std::vector<std::string> subnets;
        std::vector<int> ports{502};
        std::vector<int> unit_ids{1, 255};
        int probe_timeout_ms{50};
        int hosts_per_tick{8};
        int max_hosts_per_subnet{254};
        int minimum_prefix_length{24};
        int rescan_interval_seconds{300};
    };

    struct EndpointProbe {
        std::string host;
        int port{502};
    };

    void announce(const Target& target, int unit_id, const nlohmann::json& scan);
    void reconcileAnnouncements();
    bool hasConfiguredModule(const Target& target, int unit_id) const;
    void refreshNetworkProbeQueue();
    void probeNetworkEndpoints();
    void addNetworkTarget(const EndpointProbe& endpoint);

    DARTWIC::API::SDK_API* api_{};
    std::vector<Target> targets_;
    std::size_t target_index_{};
    std::size_t unit_index_{};
    std::vector<std::string> announced_ids_;
    std::unordered_map<std::string, std::string> request_ids_;
    NetworkScan network_scan_;
    std::vector<EndpointProbe> network_probe_queue_;
    std::size_t network_probe_index_{};
    std::chrono::steady_clock::time_point next_network_scan_{};
    std::chrono::steady_clock::time_point next_announcement_error_{};
    mutable std::mutex mutex_;
};

std::shared_ptr<ModbusDeviceFinder> createModbusDeviceFinder(
    DARTWIC::API::SDK_API* api,
    const nlohmann::json& plugin_config);
