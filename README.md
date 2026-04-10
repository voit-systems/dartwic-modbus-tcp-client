# dartwic-modbus-tcp-client

Modbus TCP plugin package fixture for the new DARTWIC plugin system.

## Layout

- `engine/`
  - source for the engine plugin DLL
  - `plugin.json` is the source manifest
  - `src/module_config.json` describes the repeatable module type exposed by the plugin
- `interface/`
  - source for the interface plugin
  - `plugin.json` is the source manifest
- `package/`
  - generated release output only

## Build

- Interface: run `node build.mjs`
- Engine: configure and build the root `CMakeLists.txt`
- Interface dev install: run `npm run install:dev`

The outputs are written to:

- `package/engine-plugin/modbus_tcp_client`
- `package/interface-plugin/modbus_tcp_client`

`build-configuration.json` controls optional copy behavior:

- `copy_package`: when `true`, the packaged plugin outputs are also copied to external DARTWIC roots
- `engine_dir`: engine root that receives `plugins/<plugin-id>`
- `interface_dir`: interface root that receives `plugins/<plugin-id>`

## Runtime Model

- The engine plugin is a singleton plugin object loaded from `plugins/modbus_tcp_client`.
- It registers the Modbus task types on plugin load.
- It exposes one repeatable module type: `modbus_tcp_client`.
- Module instances live separately under `modules/*.json`.
