#ifndef MODBUS_REGISTER_SCANNER_H
#define MODBUS_REGISTER_SCANNER_H

#include <nlohmann/json.hpp>

nlohmann::json scanModbusRegisters(const nlohmann::json& request);

#endif // MODBUS_REGISTER_SCANNER_H
