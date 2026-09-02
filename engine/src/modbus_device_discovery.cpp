#include "modbus_device_discovery.h"

#include "modbus_register_scanner.h"

#include <modules/BaseModule.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
struct IPv4Subnet {
    std::uint32_t address{};
    int prefix_length{24};
};

std::uint32_t prefixMask(const int prefix_length) {
    if (prefix_length <= 0) return 0;
    if (prefix_length >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefix_length);
}

bool parseIPv4(const std::string& text, std::uint32_t& address) {
    in_addr parsed{};
    if (inet_pton(AF_INET, text.c_str(), &parsed) != 1) return false;
    address = ntohl(parsed.s_addr);
    return true;
}

std::string formatIPv4(const std::uint32_t address) {
    in_addr value{};
    value.s_addr = htonl(address);
    char text[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &value, text, sizeof(text)) == nullptr) return {};
    return text;
}

bool parseCidr(const std::string& cidr, IPv4Subnet& subnet) {
    const auto separator = cidr.find('/');
    const std::string address_text = cidr.substr(0, separator);
    int prefix_length = 24;
    if (separator != std::string::npos) {
        try {
            prefix_length = std::stoi(cidr.substr(separator + 1));
        } catch (...) {
            return false;
        }
    }
    if (prefix_length < 0 || prefix_length > 32 || !parseIPv4(address_text, subnet.address)) return false;
    subnet.prefix_length = prefix_length;
    return true;
}

std::vector<IPv4Subnet> localIPv4Subnets() {
    std::vector<IPv4Subnet> subnets;
#ifdef _WIN32
    ULONG buffer_size = 15 * 1024;
    std::vector<unsigned char> buffer(buffer_size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG result = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr,
        adapters,
        &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buffer_size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,
            adapters,
            &buffer_size);
    }
    if (result != NO_ERROR) return subnets;
    for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* socket_address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            subnets.push_back({ntohl(socket_address->sin_addr.s_addr), unicast->OnLinkPrefixLength});
        }
    }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return subnets;
    for (auto* interface = interfaces; interface != nullptr; interface = interface->ifa_next) {
        if (interface->ifa_addr == nullptr || interface->ifa_addr->sa_family != AF_INET) continue;
        if ((interface->ifa_flags & IFF_UP) == 0 || (interface->ifa_flags & IFF_LOOPBACK) != 0) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(interface->ifa_addr);
        const auto* netmask = reinterpret_cast<const sockaddr_in*>(interface->ifa_netmask);
        const std::uint32_t mask = netmask ? ntohl(netmask->sin_addr.s_addr) : prefixMask(24);
        int prefix_length = 0;
        for (std::uint32_t bit = 0x80000000u; (mask & bit) != 0; bit >>= 1) ++prefix_length;
        subnets.push_back({ntohl(address->sin_addr.s_addr), prefix_length});
    }
    freeifaddrs(interfaces);
#endif
    return subnets;
}

bool tcpPortOpen(const std::string& host, const int port, const int timeout_ms) {
#ifdef _WIN32
    static std::once_flag winsock_once;
    static bool winsock_ready = false;
    std::call_once(winsock_once, []() {
        WSADATA data{};
        winsock_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    if (!winsock_ready) return false;
    const SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) return false;
    u_long nonblocking = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) != 0) {
        closesocket(socket_handle);
        return false;
    }
#else
    const int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle < 0) return false;
    const int original_flags = fcntl(socket_handle, F_GETFL, 0);
    if (original_flags < 0 || fcntl(socket_handle, F_SETFL, original_flags | O_NONBLOCK) != 0) {
        close(socket_handle);
        return false;
    }
#endif
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, host.c_str(), &endpoint.sin_addr) != 1) {
#ifdef _WIN32
        closesocket(socket_handle);
#else
        close(socket_handle);
#endif
        return false;
    }
    const int connected = connect(socket_handle, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
    if (connected != 0) {
#ifdef _WIN32
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
            closesocket(socket_handle);
            return false;
        }
#else
        if (errno != EINPROGRESS) {
            close(socket_handle);
            return false;
        }
#endif
    }
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(socket_handle, &write_set);
    timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#ifdef _WIN32
    const int selected = select(0, nullptr, &write_set, nullptr, &timeout);
#else
    const int selected = select(socket_handle + 1, nullptr, &write_set, nullptr, &timeout);
#endif
    int socket_error = 0;
#ifdef _WIN32
    int error_length = sizeof(socket_error);
#else
    socklen_t error_length = sizeof(socket_error);
#endif
    const bool open = selected > 0 && getsockopt(
        socket_handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &error_length) == 0 &&
        socket_error == 0;
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
    return open;
}

std::string safeName(std::string value) {
    for (char& character : value) {
        if (!std::isalnum(static_cast<unsigned char>(character))) character = '_';
        else character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    while (value.find("__") != std::string::npos) value.replace(value.find("__"), 2, "_");
    if (value.empty() || !std::isalpha(static_cast<unsigned char>(value.front()))) value = "device_" + value;
    return value;
}

void appendChannels(nlohmann::json& channels, nlohmann::json& mappings,
    const nlohmann::json& table, const std::string& register_type, const std::string& channel_segment,
    const std::string& direction, const std::string& instance_name) {
    if (!table.value("supported", false)) return;
    for (const auto& range : table.value("ranges", nlohmann::json::array())) {
        const int start = range.value("start", 0);
        const int end = range.value("end", -1);
        for (int address = start; address <= end && channels.size() < 4096; ++address) {
            const std::string channel = instance_name + "." + channel_segment + "." + std::to_string(address);
            channels.push_back({
                {"name", channel},
                {"direction", direction},
                {"register_type", register_type},
                {"address", address}
            });
            mappings.push_back({
                {"register", address},
                {"register_type", register_type},
                {"channel", channel}
            });
        }
    }
}

nlohmann::json suggestionGroup(const nlohmann::json& table, const char* id, const char* label,
    const char* register_type, const char* channel_segment, int probed_count,
    bool auto_map_responding_addresses, bool writable) {
    const int response_count = table.value("available_count", 0);
    const bool supported = table.value("supported", false);
    const bool full_probe_response = supported && probed_count > 0 && response_count == probed_count;
    const bool use_as_default = auto_map_responding_addresses && supported && response_count > 0;
    auto task_options = nlohmann::json::array({{
        {"task_id", "read"},
        {"label", "Read"},
        {"channel_segment", channel_segment},
        {"direction", "input"},
        {"default_enabled", use_as_default && !writable}
    }});
    if (writable) {
        task_options.push_back({
            {"task_id", "write"},
            {"label", "Write"},
            {"channel_segment", std::string(channel_segment) + "_command"},
            {"direction", "output"},
            {"default_enabled", use_as_default}
        });
    }
    return {
        {"id", id},
        {"label", label},
        {"register_type", register_type},
        {"channel_segment", channel_segment},
        {"direction", "input"},
        {"supported", supported},
        {"response_count", response_count},
        {"response_ranges", table.value("ranges", nlohmann::json::array())},
        {"full_probe_response", full_probe_response},
        {"task_options", std::move(task_options)},
        {"default_ranges", supported
            ? table.value("ranges", nlohmann::json::array())
            : nlohmann::json::array()}
    };
}
}

ModbusDeviceFinder::ModbusDeviceFinder(DARTWIC::API::SDK_API* api, const nlohmann::json& plugin_config)
    : api_(api) {
    const auto discovery = plugin_config.value("device_discovery", nlohmann::json::object());
    if (!discovery.value("enabled", true)) return;
    const auto network_scan = discovery.value("network_scan", nlohmann::json::object());
    network_scan_.enabled = network_scan.value("enabled", network_scan_.enabled);
    network_scan_.include_local_subnets = network_scan.value(
        "include_local_subnets", network_scan_.include_local_subnets);
    network_scan_.subnets = network_scan.value("subnets", network_scan_.subnets);
    network_scan_.ports = network_scan.value("ports", network_scan_.ports);
    network_scan_.unit_ids = network_scan.value("unit_ids", network_scan_.unit_ids);
    network_scan_.probe_timeout_ms = std::clamp(
        network_scan.value("probe_timeout_ms", network_scan_.probe_timeout_ms), 10, 2000);
    network_scan_.hosts_per_tick = std::clamp(
        network_scan.value("hosts_per_tick", network_scan_.hosts_per_tick), 1, 64);
    network_scan_.max_hosts_per_subnet = std::clamp(
        network_scan.value("max_hosts_per_subnet", network_scan_.max_hosts_per_subnet), 1, 4096);
    network_scan_.minimum_prefix_length = std::clamp(
        network_scan.value("minimum_prefix_length", network_scan_.minimum_prefix_length), 16, 30);
    network_scan_.rescan_interval_seconds = std::clamp(
        network_scan.value("rescan_interval_seconds", network_scan_.rescan_interval_seconds), 10, 86400);
    network_scan_.ports.erase(std::remove_if(network_scan_.ports.begin(), network_scan_.ports.end(),
        [](const int port) { return port < 1 || port > 65535; }), network_scan_.ports.end());
    network_scan_.unit_ids.erase(std::remove_if(network_scan_.unit_ids.begin(), network_scan_.unit_ids.end(),
        [](const int unit_id) { return unit_id < 0 || (unit_id > 247 && unit_id != 255); }),
        network_scan_.unit_ids.end());
    if (network_scan_.ports.empty()) network_scan_.ports = {502};
    if (network_scan_.unit_ids.empty()) network_scan_.unit_ids = {1, 255};

    const auto configured_targets = discovery.value("targets", nlohmann::json::array());
    if (configured_targets.empty()) {
        targets_.push_back({});
        return;
    }
    for (const auto& item : configured_targets) {
        if (!item.is_object()) continue;
        Target target;
        target.host = item.value("host", target.host);
        target.port = item.value("port", target.port);
        target.unit_ids = item.value("unit_ids", target.unit_ids);
        target.start_address = item.value("start_address", target.start_address);
        target.end_address = item.value("end_address", target.end_address);
        target.timeout_ms = item.value("timeout_ms", target.timeout_ms);
        target.scan_all_unit_ids = item.value("scan_all_unit_ids", target.scan_all_unit_ids);
        target.auto_map_responding_addresses = item.value(
            "auto_map_responding_addresses", target.auto_map_responding_addresses);
        if (!target.host.empty() && target.port > 0 && target.port <= 65535 && !target.unit_ids.empty()) {
            targets_.push_back(std::move(target));
        }
    }
}

void ModbusDeviceFinder::refreshNetworkProbeQueue() {
    network_probe_queue_.clear();
    network_probe_index_ = 0;
    if (!network_scan_.enabled) return;

    std::vector<IPv4Subnet> subnets;
    std::unordered_set<std::uint32_t> local_addresses;
    if (network_scan_.include_local_subnets) {
        auto local_subnets = localIPv4Subnets();
        for (const auto& subnet : local_subnets) local_addresses.insert(subnet.address);
        subnets.insert(subnets.end(), local_subnets.begin(), local_subnets.end());
    }
    for (const auto& configured_subnet : network_scan_.subnets) {
        IPv4Subnet subnet;
        if (parseCidr(configured_subnet, subnet)) subnets.push_back(subnet);
    }

    std::unordered_set<std::string> seen_subnets;
    std::unordered_set<std::string> seen_endpoints;
    for (const auto& target : targets_) {
        seen_endpoints.insert(target.host + ":" + std::to_string(target.port));
    }
    for (auto subnet : subnets) {
        subnet.prefix_length = std::max(subnet.prefix_length, network_scan_.minimum_prefix_length);
        if (subnet.prefix_length > 30) continue;
        const auto mask = prefixMask(subnet.prefix_length);
        const auto network = subnet.address & mask;
        const std::string subnet_key = formatIPv4(network) + "/" + std::to_string(subnet.prefix_length);
        if (!seen_subnets.insert(subnet_key).second) continue;
        const std::uint64_t address_count = std::uint64_t{1} << (32 - subnet.prefix_length);
        const std::uint64_t host_count = std::min<std::uint64_t>(
            address_count - 2, static_cast<std::uint64_t>(network_scan_.max_hosts_per_subnet));
        for (std::uint64_t offset = 1; offset <= host_count; ++offset) {
            const auto address = network + static_cast<std::uint32_t>(offset);
            const auto first_octet = address >> 24;
            if (first_octet == 0 || first_octet == 127 || first_octet >= 224 || local_addresses.contains(address)) {
                continue;
            }
            const auto host = formatIPv4(address);
            if (host.empty()) continue;
            for (const int port : network_scan_.ports) {
                const std::string endpoint_key = host + ":" + std::to_string(port);
                if (seen_endpoints.insert(endpoint_key).second) {
                    network_probe_queue_.push_back({host, port});
                }
            }
        }
    }
    next_network_scan_ = std::chrono::steady_clock::now() +
        std::chrono::seconds(network_scan_.rescan_interval_seconds);
}

void ModbusDeviceFinder::addNetworkTarget(const EndpointProbe& endpoint) {
    const auto exists = std::any_of(targets_.begin(), targets_.end(), [&](const Target& target) {
        return target.host == endpoint.host && target.port == endpoint.port;
    });
    if (exists) return;
    Target target;
    if (!targets_.empty()) {
        target.start_address = targets_.front().start_address;
        target.end_address = targets_.front().end_address;
        target.timeout_ms = targets_.front().timeout_ms;
        target.scan_all_unit_ids = targets_.front().scan_all_unit_ids;
        target.auto_map_responding_addresses = targets_.front().auto_map_responding_addresses;
    }
    target.host = endpoint.host;
    target.port = endpoint.port;
    target.unit_ids = network_scan_.unit_ids;
    target.network_discovered = true;
    targets_.push_back(std::move(target));
}

void ModbusDeviceFinder::probeNetworkEndpoints() {
    if (!network_scan_.enabled) return;
    const auto now = std::chrono::steady_clock::now();
    if (network_probe_queue_.empty()) {
        if (next_network_scan_ == std::chrono::steady_clock::time_point{} || now >= next_network_scan_) {
            refreshNetworkProbeQueue();
        } else {
            return;
        }
    }
    int processed = 0;
    while (network_probe_index_ < network_probe_queue_.size() && processed < network_scan_.hosts_per_tick) {
        const auto endpoint = network_probe_queue_[network_probe_index_++];
        if (tcpPortOpen(endpoint.host, endpoint.port, network_scan_.probe_timeout_ms)) {
            addNetworkTarget(endpoint);
        }
        ++processed;
    }
    if (network_probe_index_ >= network_probe_queue_.size()) {
        network_probe_queue_.clear();
        network_probe_index_ = 0;
    }
}

void ModbusDeviceFinder::reconcileAnnouncements() {
    for (auto announced = announced_ids_.begin(); announced != announced_ids_.end();) {
        const auto request_id = request_ids_.find(*announced);
        if (request_id == request_ids_.end()) {
            announced = announced_ids_.erase(announced);
            continue;
        }
        try {
            const auto request = api_->getInterfaceUiRequest(request_id->second);
            const std::string status = request.value("status", "pending");
            if (status == "pending") {
                ++announced;
                continue;
            }
            // A terminal UI request is not proof that the endpoint is configured.
            // Live module configuration is checked separately on every finder pass.
            request_ids_.erase(request_id);
            announced = announced_ids_.erase(announced);
        } catch (const std::exception&) {
            // Preserve the announcement on transient broker failures to avoid duplicate prompts.
            ++announced;
        }
    }
}

bool ModbusDeviceFinder::hasConfiguredModule(const Target& target, const int unit_id) const {
    for (const auto& summary : api_->getModuleInstances("modbus_tcp_client")) {
        const auto module = api_->getModuleInstance(summary.name);
        if (!module) continue;

        const std::string configured_host = module->getParameter<std::string>("server_ip");
        const int configured_port = module->getParameter<int>("server_port", 502);
        if (configured_host != target.host || configured_port != target.port) continue;

        if (!target.scan_all_unit_ids || module->getParameter<int>("unit_id", 255) == unit_id) {
            return true;
        }
    }
    return false;
}

void ModbusDeviceFinder::tick() {
    std::scoped_lock lock(mutex_);
    if (api_ == nullptr) return;
    reconcileAnnouncements();
    probeNetworkEndpoints();
    if (targets_.empty()) return;
    const auto& target = targets_[target_index_];
    const int unit_id = target.unit_ids[unit_index_];
    const std::string endpoint_id = target.host + ":" + std::to_string(target.port);
    const std::string discovery_id = target.scan_all_unit_ids
        ? endpoint_id + ":" + std::to_string(unit_id)
        : endpoint_id;
    const bool already_configured = hasConfiguredModule(target, unit_id);
    const bool already_announced = std::find(
        announced_ids_.begin(), announced_ids_.end(), discovery_id) != announced_ids_.end();
    if (!already_configured && !already_announced) {
        nlohmann::json scan;
        try {
            scan = scanModbusRegisters({
                {"server_ip", target.host},
                {"server_port", target.port},
                {"unit_id", unit_id},
                {"start_address", target.start_address},
                {"end_address", target.end_address},
                {"timeout_ms", target.timeout_ms}
            });
        } catch (const std::exception&) {
            // Connection failures, unsupported unit IDs, and absent endpoints are
            // normal while probing and must not create operator-facing errors.
        }
        if (scan.is_object()) {
            try {
                announce(target, unit_id, scan);
            } catch (const std::exception& error) {
                const auto now = std::chrono::steady_clock::now();
                if (next_announcement_error_ == std::chrono::steady_clock::time_point{} ||
                    now >= next_announcement_error_) {
                    api_->consoleError(
                        "MODBUS DISCOVERY UI ERROR",
                        error.what(),
                        {},
                        "Verify the interface UI request broker and module-discovery registration.",
                        0);
                    next_announcement_error_ = now + std::chrono::seconds(30);
                }
            }
        }
    }
    unit_index_ = (unit_index_ + 1) % target.unit_ids.size();
    if (unit_index_ == 0) target_index_ = (target_index_ + 1) % targets_.size();
}

nlohmann::json ModbusDeviceFinder::settings() const {
    std::scoped_lock lock(mutex_);
    const Target defaults = targets_.empty() ? Target{} : targets_.front();
    return {
        {"enabled", !targets_.empty() || network_scan_.enabled},
        {"start_address", defaults.start_address},
        {"end_address", defaults.end_address},
        {"maximum_span", 4096},
        {"network_scan", {
            {"enabled", network_scan_.enabled},
            {"include_local_subnets", network_scan_.include_local_subnets},
            {"subnets", network_scan_.subnets},
            {"ports", network_scan_.ports},
            {"unit_ids", network_scan_.unit_ids},
            {"probe_timeout_ms", network_scan_.probe_timeout_ms},
            {"hosts_per_tick", network_scan_.hosts_per_tick},
            {"max_hosts_per_subnet", network_scan_.max_hosts_per_subnet},
            {"minimum_prefix_length", network_scan_.minimum_prefix_length},
            {"rescan_interval_seconds", network_scan_.rescan_interval_seconds},
            {"queued_endpoints", network_probe_queue_.size() - std::min(network_probe_index_, network_probe_queue_.size())}
        }}
    };
}

nlohmann::json ModbusDeviceFinder::configureAddressRange(const nlohmann::json& request) {
    if (!request.is_object() || !request.contains("start_address") || !request.contains("end_address") ||
        !request.at("start_address").is_number_integer() || !request.at("end_address").is_number_integer()) {
        throw std::runtime_error("start_address and end_address must be integers.");
    }
    const int start_address = request.at("start_address").get<int>();
    const int end_address = request.at("end_address").get<int>();
    if (start_address < 0 || end_address > 0xFFFF || end_address < start_address) {
        throw std::runtime_error("Scan addresses must be an ordered range between 0 and 65535.");
    }
    if (end_address - start_address + 1 > 4096) {
        throw std::runtime_error("The discovery scan range is limited to 4096 addresses.");
    }

    std::scoped_lock lock(mutex_);
    if (request.contains("network_scan")) {
        const auto& network = request.at("network_scan");
        if (!network.is_object()) throw std::runtime_error("network_scan must be an object.");
        NetworkScan updated = network_scan_;
        updated.enabled = network.value("enabled", updated.enabled);
        updated.include_local_subnets = network.value("include_local_subnets", updated.include_local_subnets);
        updated.subnets = network.value("subnets", updated.subnets);
        updated.ports = network.value("ports", updated.ports);
        updated.unit_ids = network.value("unit_ids", updated.unit_ids);
        updated.probe_timeout_ms = network.value("probe_timeout_ms", updated.probe_timeout_ms);
        updated.hosts_per_tick = network.value("hosts_per_tick", updated.hosts_per_tick);
        updated.max_hosts_per_subnet = network.value("max_hosts_per_subnet", updated.max_hosts_per_subnet);
        updated.minimum_prefix_length = network.value("minimum_prefix_length", updated.minimum_prefix_length);
        updated.rescan_interval_seconds = network.value(
            "rescan_interval_seconds", updated.rescan_interval_seconds);
        if (updated.probe_timeout_ms < 10 || updated.probe_timeout_ms > 2000) {
            throw std::runtime_error("Network probe timeout must be between 10 and 2000 milliseconds.");
        }
        if (updated.hosts_per_tick < 1 || updated.hosts_per_tick > 64) {
            throw std::runtime_error("hosts_per_tick must be between 1 and 64.");
        }
        if (updated.max_hosts_per_subnet < 1 || updated.max_hosts_per_subnet > 4096) {
            throw std::runtime_error("max_hosts_per_subnet must be between 1 and 4096.");
        }
        if (updated.minimum_prefix_length < 16 || updated.minimum_prefix_length > 30) {
            throw std::runtime_error("minimum_prefix_length must be between 16 and 30.");
        }
        if (updated.rescan_interval_seconds < 10 || updated.rescan_interval_seconds > 86400) {
            throw std::runtime_error("rescan_interval_seconds must be between 10 and 86400.");
        }
        if (updated.ports.empty() || std::any_of(updated.ports.begin(), updated.ports.end(),
            [](const int port) { return port < 1 || port > 65535; })) {
            throw std::runtime_error("Network scan ports must be between 1 and 65535.");
        }
        if (updated.unit_ids.empty() || std::any_of(updated.unit_ids.begin(), updated.unit_ids.end(),
            [](const int unit_id) { return unit_id < 0 || (unit_id > 247 && unit_id != 255); })) {
            throw std::runtime_error("Network scan unit IDs must be between 0 and 247, or 255.");
        }
        for (const auto& cidr : updated.subnets) {
            IPv4Subnet parsed;
            if (!parseCidr(cidr, parsed)) throw std::runtime_error("Invalid IPv4 subnet: " + cidr);
        }
        targets_.erase(std::remove_if(targets_.begin(), targets_.end(),
            [](const Target& target) { return target.network_discovered; }), targets_.end());
        target_index_ = 0;
        unit_index_ = 0;
        network_scan_ = std::move(updated);
        network_probe_queue_.clear();
        network_probe_index_ = 0;
        next_network_scan_ = {};
    }
    for (auto& target : targets_) {
        target.start_address = start_address;
        target.end_address = end_address;
    }
    announced_ids_.clear();
    request_ids_.clear();
    return {
        {"start_address", start_address},
        {"end_address", end_address},
        {"maximum_span", 4096},
        {"network_scan", {
            {"enabled", network_scan_.enabled},
            {"include_local_subnets", network_scan_.include_local_subnets},
            {"subnets", network_scan_.subnets},
            {"ports", network_scan_.ports},
            {"unit_ids", network_scan_.unit_ids},
            {"probe_timeout_ms", network_scan_.probe_timeout_ms},
            {"hosts_per_tick", network_scan_.hosts_per_tick},
            {"max_hosts_per_subnet", network_scan_.max_hosts_per_subnet},
            {"minimum_prefix_length", network_scan_.minimum_prefix_length},
            {"rescan_interval_seconds", network_scan_.rescan_interval_seconds}
        }},
        {"message", "Discovery settings updated. The next device finder pass will rescan them."}
    };
}

void ModbusDeviceFinder::announce(const Target& target, int unit_id, const nlohmann::json& scan) {
    const std::string endpoint_id = target.host + ":" + std::to_string(target.port);
    const std::string discovery_id = target.scan_all_unit_ids
        ? endpoint_id + ":" + std::to_string(unit_id)
        : endpoint_id;
    if (std::find(announced_ids_.begin(), announced_ids_.end(), discovery_id) != announced_ids_.end()) return;

    const std::string instance_name = safeName("modbus_" + target.host + "_" +
        std::to_string(target.port) + "_u" + std::to_string(unit_id));
    auto channels = nlohmann::json::array();
    auto read_mappings = nlohmann::json::array();
    auto write_mappings = nlohmann::json::array();
    const auto scanned = scan.value("scanned", nlohmann::json::object());
    const int scan_start = scanned.value("start_address", 0);
    const int scan_end = scanned.value("end_address", -1);
    const int probed_count = scan_end >= scan_start ? scan_end - scan_start + 1 : 0;
    const auto coils = suggestionGroup(scan.value("coils", nlohmann::json::object()),
        "coils", "Coils / outputs", "coil", "coil", probed_count,
        target.auto_map_responding_addresses, true);
    const auto discrete_inputs = suggestionGroup(scan.value("discrete_inputs", nlohmann::json::object()),
        "discrete_inputs", "Discrete inputs", "discrete_input", "discrete_input", probed_count,
        target.auto_map_responding_addresses, false);
    const auto holding_registers = suggestionGroup(scan.value("holding_registers", nlohmann::json::object()),
        "holding_registers", "Holding registers", "holding_register", "holding_register", probed_count,
        target.auto_map_responding_addresses, true);
    const auto input_registers = suggestionGroup(scan.value("input_registers", nlohmann::json::object()),
        "input_registers", "Input registers / analog inputs", "input_register", "input_register", probed_count,
        target.auto_map_responding_addresses, false);
    const auto suggestion_groups = nlohmann::json::array({
        coils, discrete_inputs, holding_registers, input_registers
    });

    if (target.auto_map_responding_addresses) {
        for (const auto& group : suggestion_groups) {
            const auto task_options = group.value("task_options", nlohmann::json::array());
            const auto default_option = std::find_if(task_options.begin(), task_options.end(), [](const auto& option) {
                return option.value("default_enabled", false);
            });
            if (default_option == task_options.end()) continue;
            const auto& task_option = *default_option;
            auto& mappings = task_option.value("task_id", std::string{}) == "write"
                ? write_mappings
                : read_mappings;
            appendChannels(channels, mappings, {
                {"supported", true}, {"ranges", group.value("default_ranges", nlohmann::json::array())}
            }, group.value("register_type", std::string{}),
                task_option.value("channel_segment", group.value("channel_segment", std::string{})),
                task_option.value("direction", std::string{"input"}), instance_name);
        }
    }

    auto suggested_tasks = nlohmann::json::array();
    if (!read_mappings.empty()) {
        suggested_tasks.push_back({
            {"name_suffix", "_read"},
            {"task_type", "read"},
            {"arguments", {{"read_mappings", read_mappings}}}
        });
    }
    if (!write_mappings.empty()) {
        suggested_tasks.push_back({
            {"name_suffix", "_write"},
            {"task_type", "write"},
            {"arguments", {{"write_mappings", write_mappings}}}
        });
    }

    auto discovery_request = api_->requestInterfaceUi("dartwic.module-discovery", {
        {"discovery_id", discovery_id},
        {"device_type", "modbus_tcp"},
        {"display_name", "Modbus TCP Module"},
        {"endpoint", {{"host", target.host}, {"port", target.port}, {"unit_id", unit_id}}},
        {"metadata", {
            {"scan", scan},
            {"protocol", "Modbus TCP"},
            {"mapping_confidence", "address_response_only"},
            {"note", "Responding addresses are not necessarily configured points; names, data types, and write permissions cannot be inferred from Modbus alone."}
        }},
        {"channels", channels},
        {"provisioning", {
            {"module_type", "tcp_client"},
            {"suggested_instance_name", instance_name},
            {"parameters", {
                {"server_ip", target.host}, {"server_port", target.port}, {"unit_id", unit_id},
                {"tv_sec", 0}, {"tv_usec", 200000},
                {"event_system", "SOFTWARE"}, {"event_subsystem", "MODBUS"}
            }},
            {"module_identity", {
                {"parameter_keys", target.scan_all_unit_ids
                    ? nlohmann::json::array({"server_ip", "server_port", "unit_id"})
                    : nlohmann::json::array({"server_ip", "server_port"})}
            }},
            {"channel_suggestion_editor", {
                {"kind", "address_ranges"},
                {"maximum_channels", 4096},
                {"groups", suggestion_groups},
                {"tasks", nlohmann::json::array({
                    {
                        {"id", "read"},
                        {"label", "Read"},
                        {"name_suffix", "_read"},
                        {"task_type", "read"},
                        {"argument_key", "read_mappings"}
                    },
                    {
                        {"id", "write"},
                        {"label", "Write"},
                        {"name_suffix", "_write"},
                        {"task_type", "write"},
                        {"argument_key", "write_mappings"}
                    }
                })}
            }},
            {"tasks", std::move(suggested_tasks)}
        }}
    }, {
        {"request_key", discovery_id},
        {"merge_key", "module-discovery"},
        {"reopen_completed", true}
    });
    const std::string request_id = discovery_request.value("request_id", "");
    if (request_id.empty()) throw std::runtime_error("The interface request broker did not return a request ID.");
    request_ids_[discovery_id] = request_id;
    announced_ids_.push_back(discovery_id);
}

std::shared_ptr<ModbusDeviceFinder> createModbusDeviceFinder(
    DARTWIC::API::SDK_API* api,
    const nlohmann::json& plugin_config) {
    return std::make_shared<ModbusDeviceFinder>(api, plugin_config);
}
