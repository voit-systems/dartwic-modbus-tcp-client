import { createModuleUiPlugin, moduleUiPluginMeta } from "./index.jsx";

(function registerModuleUiPlugin() {
    const globalRegistry = (window.__dartwicModulePluginRegistry__ = window.__dartwicModulePluginRegistry__ || {});

    globalRegistry[moduleUiPluginMeta.moduleName] = {
        createPlugin: createModuleUiPlugin
    };
})();
