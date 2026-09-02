#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include "modbus_tcp_client.h"

#include "modbus_tcp_client_module.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace {
std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
        return !is_space(static_cast<unsigned char>(character));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
        return !is_space(static_cast<unsigned char>(character));
    }).base(), value.end());
    return value;
}

bool parsePort(const std::string& value, int& port) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    })) return false;
    char* parse_end = nullptr;
    const long parsed_port = std::strtol(value.c_str(), &parse_end, 10);
    if (parse_end == value.c_str() || *parse_end != '\0' || parsed_port <= 0 || parsed_port > 65535) return false;
    port = static_cast<int>(parsed_port);
    return true;
}

std::pair<std::string, int> normalizeEndpoint(std::string server_ip, int server_port) {
    server_ip = trim(std::move(server_ip));
    const auto scheme = server_ip.find("://");
    if (scheme != std::string::npos) server_ip = server_ip.substr(scheme + 3);
    if (std::count(server_ip.begin(), server_ip.end(), ':') == 1) {
        const auto separator = server_ip.rfind(':');
        int parsed_port = server_port;
        if (separator != std::string::npos && parsePort(trim(server_ip.substr(separator + 1)), parsed_port)) {
            server_port = parsed_port;
            server_ip = server_ip.substr(0, separator);
        }
    }
    return {trim(std::move(server_ip)), server_port};
}

bool isConnectionLossError(int error_code) {
    if (error_code == ETIMEDOUT || error_code == ECONNRESET || error_code == ECONNREFUSED ||
        error_code == ENOTCONN || error_code == EPIPE || error_code == ECONNABORTED ||
        error_code == EBADF) return true;
#ifdef EHOSTUNREACH
    if (error_code == EHOSTUNREACH) return true;
#endif
#ifdef ENETDOWN
    if (error_code == ENETDOWN) return true;
#endif
#ifdef ENETRESET
    if (error_code == ENETRESET) return true;
#endif
#ifdef ENETUNREACH
    if (error_code == ENETUNREACH) return true;
#endif
#ifdef ESHUTDOWN
    if (error_code == ESHUTDOWN) return true;
#endif
    return false;
}

bool isProtocolSynchronizationError(int error_code) {
    return error_code == EMBBADDATA || error_code == EMBBADCRC ||
        error_code == EMBBADEXC || error_code == EMBBADSLAVE;
}

#ifdef _WIN32
bool isWinsockTimeout(int error_code) {
    return error_code == WSAETIMEDOUT;
}

std::string winsockErrorMessage(int error_code) {
    switch (error_code) {
        case WSAECONNRESET: return "Connection reset by the Modbus server";
        case WSAECONNABORTED: return "Connection aborted";
        case WSAECONNREFUSED: return "Connection refused";
        case WSAENOTCONN: return "Socket is no longer connected";
        case WSAESHUTDOWN: return "Socket was shut down";
        case WSAETIMEDOUT: return "Socket operation timed out";
        case WSAENETDOWN: return "Network is unavailable";
        case WSAENETRESET: return "Connection was reset by the network";
        case WSAENETUNREACH: return "Network is unreachable";
        case WSAEHOSTUNREACH: return "Modbus server is unreachable";
        case WSAENOTSOCK: return "Modbus socket is no longer valid";
        default: {
            const std::string system_message = std::system_category().message(error_code);
            return system_message.empty()
                ? "Winsock transaction failure"
                : system_message;
        }
    }
}
#endif

struct ModbusOperationError {
    int error_code{0};
    bool timeout{false};
    bool connection_lost{false};
    bool protocol_synchronization_lost{false};
    std::string message;
};

void clearModbusOperationError() {
    errno = 0;
#ifdef _WIN32
    WSASetLastError(0);
#endif
}

ModbusOperationError captureModbusOperationError() {
    ModbusOperationError error;
    error.error_code = errno;

    if (error.error_code != 0) {
        error.timeout = error.error_code == ETIMEDOUT;
        error.connection_lost = isConnectionLossError(error.error_code);
        error.protocol_synchronization_lost = isProtocolSynchronizationError(error.error_code);
        error.message = modbus_strerror(error.error_code);
        return error;
    }

#ifdef _WIN32
    // libmodbus 3.1.x does not consistently translate failed Winsock calls
    // into C errno. Preserve the native error before any logging or cleanup
    // call can overwrite it.
    const int winsock_error = WSAGetLastError();
    if (winsock_error != 0) {
        error.timeout = isWinsockTimeout(winsock_error);
        error.connection_lost = true;
        error.message = winsockErrorMessage(winsock_error) +
            " (Winsock " + std::to_string(winsock_error) + ")";
        return error;
    }
#endif

    // A -1 transaction result with no protocol or OS error is never a valid
    // Modbus exception response. The stream cannot safely be reused.
    error.connection_lost = true;
    error.message = "The Modbus server closed the connection or the socket transaction failed";
    return error;
}

bool socketPeerClosed(modbus_t* context) {
    if (context == nullptr) return true;
    const int socket = modbus_get_socket(context);
    if (socket < 0) return true;

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{};
#ifdef _WIN32
    const int selected = select(0, &readable, nullptr, nullptr, &timeout);
#else
    const int selected = select(socket + 1, &readable, nullptr, nullptr, &timeout);
#endif
    if (selected == 0) return false;
    if (selected < 0) return true;

    char byte{};
#ifdef _WIN32
    const int received = recv(static_cast<SOCKET>(socket), &byte, 1, MSG_PEEK);
    if (received == SOCKET_ERROR) return WSAGetLastError() != WSAEWOULDBLOCK;
#else
    const int received = static_cast<int>(recv(socket, &byte, 1, MSG_PEEK | MSG_DONTWAIT));
    if (received < 0) return errno != EAGAIN && errno != EWOULDBLOCK;
#endif
    return received == 0;
}
} // namespace

ModbusTCPClient::ModbusTCPClient(ModbusTCPClientModule* module,
    std::string instance_name,
    std::string server_ip,
    int server_port,
    int unit_id,
    uint32_t timeout_seconds,
    uint32_t timeout_microseconds,
    std::string event_system,
    std::string event_subsystem)
    : module_(module),
      instance_name_(std::move(instance_name)),
      server_ip_(std::move(server_ip)),
      server_port_(server_port),
      unit_id_(unit_id),
      timeout_seconds_(timeout_seconds),
      timeout_microseconds_(timeout_microseconds),
      event_system_(trim(std::move(event_system))),
      event_subsystem_(trim(std::move(event_subsystem))) {
    auto endpoint = normalizeEndpoint(std::move(server_ip_), server_port_);
    server_ip_ = std::move(endpoint.first);
    server_port_ = endpoint.second;
    if (event_system_.empty()) event_system_ = "SOFTWARE";
    if (event_subsystem_.empty()) event_subsystem_ = "MODBUS";
    configureConnectedChannel();
    setConnected(false);
}

ModbusTCPClient::~ModbusTCPClient() {
    disconnect();
}

void ModbusTCPClient::configureConnectedChannel() {
    const std::string channel = instance_name_ + ".info.connected";
    module_->dartwic->upsertChannelField(channel, DARTWIC::API::ChannelField::VALUE, 0.0,
        DARTWIC::API::ChannelStorage::Fixed);
    module_->dartwic->upsertChannelField(channel, DARTWIC::API::ChannelField::UNITS, std::string{"bool"},
        DARTWIC::API::ChannelStorage::Fixed);
    protocol_healthy_ = false;
    connection_state_published_ = true;
}

void ModbusTCPClient::setConnected(bool connected) {
    connected_ = connected;
    if (!connected) setProtocolHealthy(false);
}

void ModbusTCPClient::setProtocolHealthy(bool healthy) {
    if (connection_state_published_ && protocol_healthy_ == healthy) return;
    protocol_healthy_ = healthy;
    connection_state_published_ = true;
    module_->dartwic->upsertChannelField(instance_name_ + ".info.connected",
        DARTWIC::API::ChannelField::VALUE, healthy ? 1.0 : 0.0, DARTWIC::API::ChannelStorage::Fixed);
}

void ModbusTCPClient::markProtocolResponsive() {
    consecutive_connection_failures_ = 0;
    next_reconnect_attempt_ = {};
    setProtocolHealthy(true);
    resolveConnectionError();
}

void ModbusTCPClient::scheduleReconnect() {
    consecutive_connection_failures_ = (std::min)(
        consecutive_connection_failures_ + 1,
        static_cast<uint32_t>(30));
    const uint32_t exponent = (std::min)(consecutive_connection_failures_ - 1, static_cast<uint32_t>(5));
    const auto delay = std::chrono::seconds((std::min)(static_cast<uint32_t>(1) << exponent, static_cast<uint32_t>(30)));
    next_reconnect_attempt_ = std::chrono::steady_clock::now() + delay;
}

bool ModbusTCPClient::ensureConnected() {
    if (connected_ && context_ != nullptr) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_reconnect_attempt_) return false;
    next_reconnect_attempt_ = now + std::chrono::seconds(1);
    return connect();
}

void ModbusTCPClient::monitorConnection() {
    if (connected_ && context_ != nullptr && socketPeerClosed(context_)) {
        last_connection_error_message_ = "The Modbus TCP peer closed the connection.";
        closeContextAndSetDisconnected();
        scheduleReconnect();
    }
    if (!ensureConnected()) {
        publishConnectionError(last_connection_error_message_.empty()
            ? "Unable to establish the Modbus TCP connection."
            : last_connection_error_message_);
        return;
    }
    markProtocolResponsive();
}

bool ModbusTCPClient::connect() {
    closeContextAndSetDisconnected();
    if (server_ip_.empty() || server_port_ <= 0 || server_port_ > 65535) {
        publishConnectionError("Invalid Modbus TCP endpoint.");
        scheduleReconnect();
        return false;
    }
    if (unit_id_ < 0 || (unit_id_ > 247 && unit_id_ != 0xFF)) {
        publishConnectionError("Invalid Modbus Unit ID " + std::to_string(unit_id_) + ".");
        scheduleReconnect();
        return false;
    }
    const std::string service = std::to_string(server_port_);
    context_ = modbus_new_tcp_pi(server_ip_.c_str(), service.c_str());
    if (context_ == nullptr) {
        publishConnectionError("Unable to create Modbus TCP context.");
        return false;
    }
    if (modbus_set_slave(context_, unit_id_) == -1) {
        publishConnectionError("Invalid Modbus Unit ID " + std::to_string(unit_id_) + ".");
        closeContextAndSetDisconnected();
        scheduleReconnect();
        return false;
    }
    modbus_set_response_timeout(context_, timeout_seconds_, timeout_microseconds_);
    clearModbusOperationError();
    if (modbus_connect(context_) == -1) {
        const auto error = captureModbusOperationError();
        closeContextAndSetDisconnected();
        scheduleReconnect();
        publishConnectionError(error.message);
        return false;
    }
    ++reconnect_count_;
    setConnected(true);
    markProtocolResponsive();
    return true;
}

void ModbusTCPClient::closeContextAndSetDisconnected() {
    if (context_ != nullptr) {
        modbus_close(context_);
        modbus_free(context_);
        context_ = nullptr;
    }
    setConnected(false);
}

void ModbusTCPClient::disconnect() {
    closeContextAndSetDisconnected();
}

void ModbusTCPClient::handleOperationFailure(const std::string& operation_name) {
    ++failure_count_;
    const auto error = captureModbusOperationError();
    if (error.timeout) {
        closeContextAndSetDisconnected();
        scheduleReconnect();
        publishConnectionError(
            operation_name + ": response timeout; resetting the connection so a late reply "
            "cannot be consumed by the next transaction."
        );
        return;
    }
    if (error.connection_lost || error.protocol_synchronization_lost) {
        closeContextAndSetDisconnected();
        scheduleReconnect();
        publishConnectionError(operation_name + ": " + error.message +
            "; resetting the connection to restore transaction framing.");
    } else {
        // A Modbus exception response still proves that the TCP peer and
        // protocol endpoint are alive; the mapping itself is what failed.
        markProtocolResponsive();
        publishOperationError(operation_name, error.message);
    }
}

void ModbusTCPClient::publishConnectionError(const std::string& error_message) {
    last_connection_error_message_ = error_message.empty()
        ? "Unable to establish the Modbus TCP connection."
        : error_message;
    const auto now = std::chrono::steady_clock::now();
    if (last_connection_error_publication_.time_since_epoch().count() != 0 &&
        now - last_connection_error_publication_ < std::chrono::milliseconds(750)) return;
    last_connection_error_publication_ = now;
    const auto event = module_->dartwic->recordEvent({
        {"type", "error"},
        {"title", "MODBUS CONNECTION ERROR [" + instance_name_ + "]"},
        {"description", last_connection_error_message_ + " [" + server_ip_ + ":" +
            std::to_string(server_port_) + ", unit " + std::to_string(unit_id_) + "]"},
        {"resolution", "Verify the endpoint and device availability."},
        {"system", event_system_},
        {"subsystem", event_subsystem_},
        {"channels", {instance_name_ + ".info.connected"}},
        {"correlation_key", "modbus_connection_error|" + instance_name_},
        {"auto_acknowledge_seconds", 0},
        {"payload", {
            {"argus_record_kind", "plugin_declared_event"},
            {"plugin_event", {{"key", "modbus_connection_error|" + instance_name_}}}
        }}
    });
    connection_error_event_id_ = event.value("event_id", connection_error_event_id_);
}

void ModbusTCPClient::resolveConnectionError() {
    last_connection_error_message_.clear();
    if (connection_error_event_id_.empty()) return;
    module_->dartwic->updateEventStatus(connection_error_event_id_, "resolved");
    connection_error_event_id_.clear();
}

void ModbusTCPClient::publishOperationError(const std::string& operation_name, const std::string& error_message) {
    const auto now = std::chrono::steady_clock::now();
    if (last_operation_error_publication_.time_since_epoch().count() != 0 &&
        now - last_operation_error_publication_ < std::chrono::seconds(2)) return;
    last_operation_error_publication_ = now;
    module_->dartwic->consoleError(
        "MODBUS TRANSACTION ERROR [" + instance_name_ + "]",
        operation_name + ": " + error_message,
        {instance_name_ + ".info.connected"},
        "Verify mapped addresses, device state, and timeout settings.",
        3
    );
}

bool ModbusTCPClient::readInputRegisters(int start_address, std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_read_input_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_input_registers"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}

bool ModbusTCPClient::readDiscreteInputs(int start_address, std::vector<uint8_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_read_input_bits(
        context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_discrete_inputs"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}

bool ModbusTCPClient::readCoils(int start_address, std::vector<uint8_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_read_bits(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_coils"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}

bool ModbusTCPClient::readHoldingRegisters(int start_address, std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_read_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("read_holding_registers"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}

bool ModbusTCPClient::writeCoils(int start_address, const std::vector<uint8_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_write_bits(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("write_coils"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}

bool ModbusTCPClient::writeHoldingRegisters(int start_address, const std::vector<uint16_t>& values) {
    if (!ensureConnected() || values.empty()) return false;
    clearModbusOperationError();
    const int result = modbus_write_registers(context_, start_address, static_cast<int>(values.size()), values.data());
    if (result == -1) { handleOperationFailure("write_holding_registers"); return false; }
    const bool complete = result == static_cast<int>(values.size());
    if (complete) markProtocolResponsive();
    return complete;
}
