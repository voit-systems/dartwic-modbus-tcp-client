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

### Write Task Performance

Previously, the write task performed Modbus write and readback operations on every task execution. Adding holding-register support could double that work, because coil writes/readbacks and holding-register writes/readbacks are separate Modbus operations.

The write task now reduces idle Modbus traffic while keeping command confirmation responsive:

- Mappings are resolved and grouped into contiguous Modbus blocks once during task startup.
- Coil and holding-register writes are sent only when mapped DARTWIC channel values change.
- Successful writes mark only the affected block for confirmation readback.
- Confirmation readbacks run after successful writes, even when periodic readback is disabled.
- Periodic readbacks are controlled by `readback_interval_seconds`.
- Setting `readback_interval_seconds` to `0` disables periodic readback, but not write confirmation readback.
- State channels are written every time a Modbus readback is received, even if the value has not changed.
- State channel stale timeout is set to twice the periodic readback interval, or `0` when periodic readback is disabled.

In the steady state, if no mapped command values are changing and periodic readback is disabled, the write task performs no Modbus write or readback operations.

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

### Build Configuration

`build-configuration.json` controls optional package-copy behavior:

- `copy_package`: When `true`, build outputs are copied into the configured external DARTWIC engine/interface registry folders. When `false`, builds only update this repository's local `package/` outputs.
- `engine_dir`: External DARTWIC engine root used for engine package copying when `copy_package` is enabled.
- `interface_dir`: External DARTWIC interface root used for interface package copying when `copy_package` is enabled.

## Interface

- Install dependencies with `npm install`
- Build the UI plugin with `npm run build`
- For local desktop-app development, run `npm run install:dev`

That writes the interface release bundle to `package/interface-module/<package_id>/ui`.

## Engine

Configure and build from the repository root so the engine release assets land in `package/engine-module/<package_id>`.
