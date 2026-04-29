# dartwic-modbus-tcp-client

Modbus TCP plugin fixture for the DARTWIC plugin system.

## Layout

- `plugin.json`
  - single source manifest for the engine and interface sides
  - declares `minEngineVersion` and `minInterfaceVersion`
- `engine/`
  - source for the engine plugin DLL
  - `src/module_config.json` describes the module name, title, icon, and description
  - `src/default_parameters.json` contains the default parameter values for newly created module instances
- `interface/`
  - source for the interface plugin
- `plugin/`
  - generated release output only

## Build

- Interface: run `node build.mjs`
- Engine: configure and build the root `CMakeLists.txt`
- Interface dev install: run `npm run install:dev`

The outputs are written to:

- `plugin/engine/modbus_tcp_client`
- `plugin/interface/modbus_tcp_client`

`build-configuration.json` controls optional copy behavior:

- `copy_plugin`: when `true`, the plugin outputs are also copied to external DARTWIC roots
- `engine_dir`: engine root that receives `plugins/<plugin-id>`
- `interface_dir`: interface root that receives `plugins/<plugin-id>`

## Runtime Model

- The engine plugin is a singleton plugin object loaded from `plugins/modbus_tcp_client`.
- It registers the Modbus task types on plugin load.
- It exposes one repeatable module type: `modbus_tcp_client`.
- Module instances live separately under `modules/*.json`.

## Compatibility

- engine loading is gated by `minEngineVersion`
- interface loading is gated by `minInterfaceVersion`
- both generated plugin output folders copy the root `plugin.json`
