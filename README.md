# dartwic-modbus-tcp-client

Modbus TCP Client package for DARTWIC.

## Usage

This package provides a Modbus TCP engine module that manages Modbus client connections and exposes one task type:

- `modbus.read_input_registers`
- `modbus.write`

Use it by first creating a `modbus_tcp_client` module instance, configuring its connection settings, and then attaching `modbus.read_input_registers` tasks to that instance.

### Engine Module Configuration

The engine module exposes these configuration parameters:

- `server_ip`: Modbus TCP server IP or hostname. Default: `127.0.0.1`
- `server_port`: Modbus TCP server port. Default: `502`
- `tv_sec`: Response timeout seconds. Default: `3`
- `tv_usec`: Response timeout microseconds. Default: `0`

### Task Usage

The `modbus.read_input_registers` task expects:

- `module_instance_name`: The target Modbus TCP client module instance to use
- `mappings`: A list of register-to-channel mappings

Each mapping contains:

- `register`: The Modbus input register address to read
- `channel`: The DARTWIC channel name to publish that register value to

The `modbus.write` task expects:

- `module_instance_name`: The target Modbus TCP client module instance to use
- `readback_interval_seconds`: How often to bulk-read state while idle. Set to `0` or lower to disable periodic readback; successful writes still trigger a confirmation readback
- `mappings`: A list of register-to-channel mappings

Each mapping contains:

- `register_type`: The Modbus register type to write. Supported values are `coil` and `holding_register`
- `register`: The Modbus coil or holding register address to write
- `channel`: The DARTWIC channel whose current value should be written to the target register

Coil mappings treat any non-zero channel value as `true` and write `0` as `false`. Holding-register mappings write the channel value as a 16-bit unsigned register value. Write tasks skip bulk writes when mapped channel values have not changed, read back successfully written blocks for confirmation, and throttle idle state readbacks by `readback_interval_seconds`.

In practice, a typical setup flow is:

1. Create a `modbus_tcp_client` engine module instance.
2. Set its `server_ip`, `server_port`, `tv_sec`, and `tv_usec` values.
3. Create a `modbus.read_input_registers` task.
4. Select the module instance in `module_instance_name`.
5. Add one or more `mappings` entries from Modbus input registers to DARTWIC channels.
6. For output control, create a `modbus.write` task and add one or more coil or holding-register mappings.

## Build Layout

The repository now builds from the root:

- Engine: root `CMakeLists.txt` with root `vcpkg.json`
- Interface: root `package.json` with root `build.mjs`
- Package metadata: root `package-info.json`

The engine source remains in `engine/`, and the interface source remains in `interface/`.

## Interface

- Install dependencies with `npm install`
- Build the UI plugin with `npm run build`
- For local desktop-app development, run `npm run install:dev`

That writes the interface release bundle to `package/interface-module/<package_id>/ui`.

## Engine

Configure and build from the repository root so the engine release assets land in `package/engine-module/<package_id>`.
