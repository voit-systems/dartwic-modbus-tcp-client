# Interface Plugin

This folder is the source for the interface-side Modbus TCP plugin.

- `../plugin.json` is the source manifest
- `src/plugin.ts` default-exports the interface plugin definition
- `src/runtime.ts` registers the plugin into `window.__dartwicPluginRegistry__`
- the manifest compatibility model uses `minInterfaceVersion`

`node ..\build.mjs` writes the release bundle to:

- `plugin/interface/modbus_tcp_client/plugin.json`
- `plugin/interface/modbus_tcp_client/ui/index.js`
