import { definePlugin } from "@dartwic/interface-sdk";
import { moduleConfigs } from "./moduleConfigs";
import { taskCards } from "./taskCards";
import { taskConfigs } from "./taskConfigs";
import {ModbusPluginSettings} from "./pluginSettings";

export default definePlugin({
  id: "modbus_tcp_client",
  name: "Modbus TCP Client",
  register(registry) {
    registry.addTaskUi({
      id: "read",
      name: "Modbus Read",
      card: taskCards[0].component,
      editor: taskConfigs[0].component,
    });
    registry.addTaskUi({
      id: "write",
      name: "Modbus Write",
      card: taskCards[1].component,
      editor: taskConfigs[1].component,
    });
    registry.addModuleUi({
      id: "tcp_client",
      name: "Modbus TCP Client",
      panel: moduleConfigs[0].component,
      connection: ({instanceConfig}) => ({
        channel: `${instanceConfig.name}.info.connected`,
        label: "Modbus TCP",
        endpoint: `${instanceConfig.parameters?.server_ip || "127.0.0.1"}:${instanceConfig.parameters?.server_port ?? 502}`,
        connectedValue: 1,
      }),
    });
    registry.addSettingsPanel({
      id: "discovery",
      name: "Discovery Settings",
      component: ModbusPluginSettings,
    });
  },
});
