//
// Created by kemptonburton on 11/16/2025.
//

#include "modbus_tcp_client.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <modbus_tcp_client_module.h>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
    #include <WinSock2.h>
#else
    #include <fcntl.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace {
    std::string trim(std::string value) {
        const auto is_space = [](unsigned char character) {
            return std::isspace(character) != 0;
        };

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
        })) {
            return false;
        }

        char* parse_end = nullptr;
        const long parsed_port = std::strtol(value.c_str(), &parse_end, 10);
        if (parse_end == value.c_str() || *parse_end != '\0' || parsed_port <= 0 || parsed_port > 65535) {
            return false;
        }

        port = static_cast<int>(parsed_port);
        return true;
    }

    std::pair<std::string, int> normalizeServerEndpoint(std::string server_ip, int server_port) {
        server_ip = trim(std::move(server_ip));

        const auto scheme_separator = server_ip.find("://");
        if (scheme_separator != std::string::npos) {
            server_ip = server_ip.substr(scheme_separator + 3);
        }

        if (!server_ip.empty() && server_ip.front() == '[') {
            const auto bracket_end = server_ip.find(']');
            if (bracket_end != std::string::npos) {
                const std::string bracket_host = server_ip.substr(1, bracket_end - 1);
                if (bracket_end + 1 < server_ip.size() && server_ip[bracket_end + 1] == ':') {
                    int parsed_port = server_port;
                    if (parsePort(trim(server_ip.substr(bracket_end + 2)), parsed_port)) {
                        server_port = parsed_port;
                    }
                }
                server_ip = bracket_host;
            }
        } else if (std::count(server_ip.begin(), server_ip.end(), ':') == 1) {
            const auto separator = server_ip.rfind(':');
            int parsed_port = server_port;
            if (separator != std::string::npos && parsePort(trim(server_ip.substr(separator + 1)), parsed_port)) {
                server_port = parsed_port;
                server_ip = server_ip.substr(0, separator);
            }
        }

        return {trim(std::move(server_ip)), server_port};
    }

    bool isValidPort(int port) {
        return port > 0 && port <= 65535;
    }

    bool isConnectionLossError(int error_code) {
        return error_code == ETIMEDOUT
            || error_code == ECONNRESET
            || error_code == ECONNREFUSED
            || error_code == ENOTCONN
            || error_code == EPIPE;
    }

    std::string formatConnectionError(const std::string& error_message,
        const std::string& server_ip,
        int server_port) {
        std::ostringstream stream;
        stream << error_message << " [ENDPOINT: " << server_ip << ":" << server_port << "]";
#ifdef _WIN32
        const int wsa_error = WSAGetLastError();
        if (wsa_error != 0) {
            stream << " [WSA ERROR: " << wsa_error << "]";
        }
#endif
        return stream.str();
    }

    bool isSocketStillConnected(modbus_t* ctx) {
        if (ctx == nullptr) {
            return false;
        }

        const int socket = modbus_get_socket(ctx);
        if (socket < 0) {
            return false;
        }

#ifndef _WIN32
        if (socket >= FD_SETSIZE) {
            return true;
        }
#endif

        fd_set read_set;
        timeval timeout{};
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);

        const int ready = select(socket + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready == 0) {
            return true;
        }

        if (ready < 0) {
#ifdef _WIN32
            return WSAGetLastError() == WSAEINTR;
#else
            return errno == EINTR;
#endif
        }

        char byte = 0;
        const int received = recv(socket, &byte, 1, MSG_PEEK);
        if (received > 0) {
            return true;
        }

        if (received == 0) {
            return false;
        }

#ifdef _WIN32
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
    }

    void publishModbusConnectionError(ModbusTCPClientModule* module,
        const std::string& instance_name,
        const std::string& error_message) {
        if (module == nullptr || module->dartwic == nullptr) {
            return;
        }

        const std::string title = "MODBUS TCP CLIENT CONNECTION ERROR [" + instance_name + "]";
        const std::string tag = instance_name + "/info.connected.value";
        const std::string description =
            "THE MODBUS TCP CLIENT WAS UNABLE TO CONNECT TO THE MODBUS SERVER ON THE GIVEN IP AND PORT."
            " \n[INSTANCE NAME: " + instance_name +
            "]\n[MODBUS ERROR: " + error_message + "]";

        module->dartwic->consoleError(
            title,
            description,
            {tag},
            "ENSURE THE CONFIGURED IP AND PORT IS CORRECT AND THE DEVICE IS REACHABLE.",
            6
        );
    }

    void publishModbusOperationError(ModbusTCPClientModule* module,
        const std::string& instance_name,
        const std::string& operation_name,
        const std::string& error_message) {
        if (module == nullptr || module->dartwic == nullptr) {
            return;
        }

        const std::string title = "MODBUS TCP CLIENT TASK ERROR [" + instance_name + "]";
        const std::string tag = instance_name + "/info.connected.value";
        const std::string description =
            "A MODBUS TASK OPERATION FAILED WHILE THE CLIENT WAS MARKED CONNECTED."
            " \n[INSTANCE NAME: " + instance_name +
            "]\n[OPERATION: " + operation_name +
            "]\n[MODBUS ERROR: " + error_message + "]";

        module->dartwic->consoleError(
            title,
            description,
            {tag},
            "CHECK THE REMOTE DEVICE STATE, REGISTER/COIL ADDRESS, AND CONNECTION HEALTH.",
            3
        );
    }
}

ModbusTCPClient::ModbusTCPClient(ModbusTCPClientModule* module,
    std::string instance_name,
    std::string server_ip,
    int server_port,
    uint32_t tv_sec,
    uint32_t tv_usec)
    : module_(module),
      instance_name_(std::move(instance_name)),
      server_ip_(std::move(server_ip)),
      server_port_(server_port),
      tv_sec_(tv_sec),
      tv_usec_(tv_usec) {
    auto normalized_endpoint = normalizeServerEndpoint(std::move(server_ip_), server_port_);
    server_ip_ = std::move(normalized_endpoint.first);
    server_port_ = normalized_endpoint.second;
    configureConnectedChannel();
    setConnected(0.0);

    module_->dartwic->onLoop("modbus_connection_monitor_" + instance_name_, [this]() {
        // NOT CONNECTED - attempt connection
        if (!connected_) {
            connect();

        // CONNECTED - check connection
        } else {
            checkConnection();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    });

}

ModbusTCPClient::~ModbusTCPClient() {
    disconnect();
    module_->dartwic->removeLoop("modbus_connection_monitor_" + instance_name_);
}

bool ModbusTCPClient::isConnected() const {
    return connected_.load();
}

void ModbusTCPClient::checkConnection() {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load()) {
        return;
    }

    if (!isSocketStillConnected(ctx_)) {
        publishModbusConnectionError(
            module_,
            instance_name_,
            formatConnectionError("MODBUS TCP SOCKET DISCONNECTED", server_ip_, server_port_)
        );
        closeContextAndSetDisconnected();
    } else {
        setConnected(1.0);
    }
}

std::optional<int16_t> ModbusTCPClient::readInputRegister(int address) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load()) {
        return std::nullopt;
    }

    uint16_t raw_value = 0;
    const int rc = modbus_read_input_registers(ctx_, address, 1, &raw_value);
    if (rc == -1) {
        handleOperationFailure("read_input_register");
        return std::nullopt;
    }

    return static_cast<int16_t>(raw_value);
}

std::optional<std::vector<int16_t>> ModbusTCPClient::readInputRegisterBlock(int start_address, int count) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load() || count <= 0) {
        return std::nullopt;
    }

    std::vector<uint16_t> raw_values(static_cast<size_t>(count), 0);
    const int rc = modbus_read_input_registers(ctx_, start_address, count, raw_values.data());
    if (rc == -1) {
        handleOperationFailure("read_input_register_block");
        return std::nullopt;
    }

    std::vector<int16_t> values;
    values.reserve(static_cast<size_t>(count));
    for (const uint16_t raw_value : raw_values) {
        values.push_back(static_cast<int16_t>(raw_value));
    }

    return values;
}

std::vector<std::optional<int16_t>> ModbusTCPClient::readInputRegisters(const std::vector<int>& addresses) {
    std::vector<std::optional<int16_t>> values;
    values.reserve(addresses.size());

    for (const int address : addresses) {
        values.push_back(readInputRegister(address));
    }

    return values;
}

bool ModbusTCPClient::writeCoil(int address, bool value) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load()) {
        return false;
    }

    const int rc = modbus_write_bit(ctx_, address, value ? 1 : 0);
    if (rc == -1) {
        handleOperationFailure("write_coil");
        return false;
    }

    return true;
}

bool ModbusTCPClient::writeCoilBlock(int start_address, const std::vector<uint8_t>& values) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load() || values.empty()) {
        return false;
    }

    const int rc = modbus_write_bits(ctx_, start_address, static_cast<int>(values.size()), values.data());
    if (rc == -1) {
        handleOperationFailure("write_coil_block");
        return false;
    }

    return true;
}

std::optional<uint8_t> ModbusTCPClient::readCoil(int address) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load()) {
        return std::nullopt;
    }

    uint8_t raw_value = 0;
    const int rc = modbus_read_bits(ctx_, address, 1, &raw_value);
    if (rc == -1) {
        handleOperationFailure("read_coil");
        return std::nullopt;
    }

    return static_cast<uint8_t>(raw_value);
}

std::optional<std::vector<uint8_t>> ModbusTCPClient::readCoilBlock(int start_address, int count) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load() || count <= 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> values(static_cast<size_t>(count), 0);
    const int rc = modbus_read_bits(ctx_, start_address, count, values.data());
    if (rc == -1) {
        handleOperationFailure("read_coil_block");
        return std::nullopt;
    }

    return values;
}

bool ModbusTCPClient::writeHoldingRegisterBlock(int start_address, const std::vector<uint16_t>& values) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load() || values.empty()) {
        return false;
    }

    const int rc = modbus_write_registers(ctx_, start_address, static_cast<int>(values.size()), values.data());
    if (rc == -1) {
        handleOperationFailure("write_holding_register_block");
        return false;
    }

    return true;
}

std::optional<std::vector<uint16_t>> ModbusTCPClient::readHoldingRegisterBlock(int start_address, int count) {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    if (ctx_ == nullptr || !connected_.load() || count <= 0) {
        return std::nullopt;
    }

    std::vector<uint16_t> values(static_cast<size_t>(count), 0);
    const int rc = modbus_read_registers(ctx_, start_address, count, values.data());
    if (rc == -1) {
        handleOperationFailure("read_holding_register_block");
        return std::nullopt;
    }

    return values;
}


void ModbusTCPClient::setConnected(double connected_value) {
    const bool next_connected = connected_value != 0.0;
    // set connected flag
    connected_.store(next_connected);
    // set connected channel in dartwic
    module_->dartwic->upsertChannelField(instance_name_, "info.connected", DARTWIC::API::ChannelField::VALUE, connected_value);
}

void ModbusTCPClient::configureConnectedChannel() {
    const std::string controller = "loop:modbus_connection_monitor_" + instance_name_;
    module_->dartwic->upsertChannelField(
        instance_name_,
        "info.connected",
        DARTWIC::API::ChannelField::CONTROL_POLICY,
        DARTWIC::API::ControlPolicy::ObserveOnly
    );
    module_->dartwic->upsertChannelField(
        instance_name_,
        "info.connected",
        DARTWIC::API::ChannelField::CONTROL_OWNER,
        controller
    );
    module_->dartwic->upsertChannelField(
        instance_name_,
        "info.connected",
        DARTWIC::API::ChannelField::ACTIVE_CONTROLLER,
        controller
    );
}

void ModbusTCPClient::closeContextAndSetDisconnected() {
    if (ctx_ != nullptr) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    setConnected(0.0);
}

void ModbusTCPClient::handleOperationFailure(const std::string& operation_name) {
    const int error_code = errno;
    const std::string error_message = modbus_strerror(error_code);

    if (isConnectionLossError(error_code)) {
        publishModbusConnectionError(
            module_,
            instance_name_,
            formatConnectionError(error_message, server_ip_, server_port_)
        );
        closeContextAndSetDisconnected();
        return;
    }

    publishModbusOperationError(module_, instance_name_, operation_name, error_message);
}

bool ModbusTCPClient::connect() {
    std::lock_guard<std::mutex> lock(ctx_lock_);
    setConnected(0.0);

    if (ctx_ != nullptr) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }

    /// CREATE TCP CONTEXT ///
    if (server_ip_.empty() || !isValidPort(server_port_)) {
        publishModbusConnectionError(
            module_,
            instance_name_,
            formatConnectionError("INVALID MODBUS TCP ENDPOINT", server_ip_, server_port_)
        );
        setConnected(0.0);
        return false;
    }

    const std::string server_service = std::to_string(server_port_);
    ctx_ = modbus_new_tcp_pi(server_ip_.c_str(), server_service.c_str());

    // CREATION ERROR
    if (ctx_ == nullptr) {
        std::cerr << "Unable to create Modbus TCP context." << std::endl;
        publishModbusConnectionError(
            module_,
            instance_name_,
            formatConnectionError("UNABLE TO CREATE MODBUS TCP CONTEXT", server_ip_, server_port_)
        );
        setConnected(0.0);
        return false;
    }

    modbus_set_response_timeout(ctx_, tv_sec_, tv_usec_);

    /// CONNECT TO SERVER ///
    int connect_result =  modbus_connect(ctx_);

    // CONNECTION ERROR
    if (connect_result == -1) {
        publishModbusConnectionError(
            module_,
            instance_name_,
            formatConnectionError(modbus_strerror(errno), server_ip_, server_port_)
        );

        // reset context - failed
        closeContextAndSetDisconnected();
        return false;
    }

    /// SUCCESS ///
    //set context settings
    setConnected(1.0);

    std::ostringstream stream;
    stream << "Connected to Modbus TCP server at " << server_ip_ << ":" << server_port_
           << " instance name: " << instance_name_ << std::endl;
    std::cout << stream.str() << std::endl;

    return true;
}

void ModbusTCPClient::disconnect() {
    std::lock_guard<std::mutex> lock(ctx_lock_);

    if (ctx_ != nullptr) {
        closeContextAndSetDisconnected();
    } else {
        setConnected(0.0);
    }
}
