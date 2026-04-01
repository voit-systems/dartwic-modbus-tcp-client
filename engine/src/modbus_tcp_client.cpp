//
// Created by kemptonburton on 11/16/2025.
//

#include "modbus_tcp_client.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <modbus_tcp_client_module.h>
#include <sstream>
#include <string>

#ifdef _WIN32
    #include <WinSock2.h>
#else
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace {
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
            3
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

    module_->dartwic->onLoop("modbus_connection_monitor_" + instance_name_, [this]() {

        // NOT CONNECTED - attempt connection
        if (!connected_) {
            connect();

        // CONNECTED - check connection
        } else {
            checkConnection();
        }

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

    // READ INPUT REGISTER 0
    uint16_t raw_value = 0;
    const int rc = modbus_read_input_registers(ctx_, 0, 1, &raw_value);

    // ERROR - NOT CONNECTED
    if (rc == -1) {
        publishModbusConnectionError(module_, instance_name_, modbus_strerror(errno));
        setConnected(0.0);

    // CONNECTED - keep setting setconnected to 1.0
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
        publishModbusOperationError(module_, instance_name_, "read_input_register", modbus_strerror(errno));
        return std::nullopt;
    }

    return static_cast<int16_t>(raw_value);
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
        publishModbusOperationError(module_, instance_name_, "write_coil", modbus_strerror(errno));
        return false;
    }

    return true;
}


void ModbusTCPClient::setConnected(double connected_value) {
    const bool next_connected = connected_value != 0.0;
    // set connected flag
    connected_.store(next_connected);
    // set connected channel in dartwic
    module_->dartwic->upsertChannelField(instance_name_, "info.connected", DARTWIC::API::ChannelField::VALUE, connected_value);
}

bool ModbusTCPClient::connect() {
    std::lock_guard<std::mutex> lock(ctx_lock_);

    /// CREATE TCP CONTEXT ///
    ctx_ = modbus_new_tcp(server_ip_.c_str(), server_port_);

    // CREATION ERROR
    if (ctx_ == nullptr) {
        std::cerr << "Unable to create Modbus TCP context." << std::endl;
        publishModbusConnectionError(module_, instance_name_, "UNABLE TO CREATE MODBUS TCP CONTEXT");
        setConnected(0.0);
        return false;
    }

    /// CONNECT TO SERVER ///
    int connect_result =  modbus_connect(ctx_);

    // CONNECTION ERROR
    if (connect_result == -1) {
        publishModbusConnectionError(module_, instance_name_, modbus_strerror(errno));

        // reset context - failed
        modbus_free(ctx_);
        ctx_ = nullptr;
        setConnected(0.0);
        return false;
    }

    /// SUCCESS ///
    //set context settings
    modbus_set_response_timeout(ctx_, tv_sec_, tv_usec_);
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
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    setConnected(0.0);
}
