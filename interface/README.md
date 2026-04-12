# Interface Plugin

This folder is the source for the interface-side Modbus TCP plugin.

- `../plugin.json` is the source manifest
- `src/index.jsx` exports the UI plugin factory
- `src/runtime-entry.jsx` registers the plugin into `window.__dartwicPluginRegistry__`

`node ..\build.mjs` writes the release bundle to:

- `plugin/interface/modbus_tcp_client/plugin.json`
- `plugin/interface/modbus_tcp_client/ui/index.js`
