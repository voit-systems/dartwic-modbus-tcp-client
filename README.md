# dartwic-modbus-tcp-client

Modbus TCP plugin fixture for the DARTWIC plugin system.

## Layout

- `plugin.json`
  - single source manifest for the engine and interface sides
  - declares `minEngineVersion` and `minInterfaceVersion`
- `versions.json`
  - generated compatibility history keyed by released plugin version
- `engine/`
  - source for the engine plugin DLL
  - `src/module_config.json` describes the module name, title, icon, and description
  - `src/default_parameters.json` contains the default parameter values for newly created module instances
- `interface/`
  - source for the interface plugin
- `plugin/`
  - generated release output only

## Build

- Initialize the bundled `vcpkg` submodule after clone:
  - `git submodule update --init --recursive`
- Interface: run `npm run build`
- Engine: configure and build the root `CMakeLists.txt`, or use the provided package/deploy commands

The outputs are written to:

- `plugin/engine/modbus_tcp_client`
- `plugin/engine-debug/modbus_tcp_client` when built in Debug or during `npm run package`
- `plugin/interface/modbus_tcp_client`

## Versioning and packaging

- `npm version patch|minor|major|<exact-version>` updates `package.json`, syncs `plugin.json`, and records the current compatibility floor in `versions.json`
- `npm run package` builds release engine, attempts debug engine, builds interface when present, and writes `plugin.zip`
- `npm run package -- 0.8.1` bumps to that exact version before packaging

`deployment-settings.json` controls explicit deploy targets:

- `engine_dir`: engine root that receives `plugins/<plugin-id>`
- `interface_dir`: interface root that receives `plugins/<plugin-id>`

Deploy commands:

- `npm run deploy` builds and deploys the release engine variant plus the interface plugin
- `npm run deploy:engine-debug` builds and deploys only the debug engine variant
- `npm run deploy:debug` builds and deploys the debug engine variant plus the interface plugin
- `npm run deploy:interface` builds and deploys only the interface plugin

Build behavior:

- plain `cmake` engine builds never copy into external DARTWIC plugin directories
- plain `npm run build` never copies into external DARTWIC plugin directories
- only the explicit `deploy*` commands copy to paths from `deployment-settings.json`

## Runtime Model

- The engine plugin is a singleton plugin object loaded from `plugins/modbus_tcp_client`.
- It registers the Modbus task types on plugin load.
- It exposes one repeatable module type: `modbus_tcp_client`.
- Module instances live separately under `modules/*.json`.

## Compatibility

- engine loading is gated by `minEngineVersion`
- interface loading is gated by `minInterfaceVersion`
- both generated plugin output folders copy the root `plugin.json`
- release DARTWIC consumes only `plugin/engine/modbus_tcp_client`
- debug DARTWIC consumes only `plugin/engine-debug/modbus_tcp_client`
- there is no fallback between release and debug engine variants
