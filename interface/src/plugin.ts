import { definePlugin } from "@dartwic/interface-sdk";
import { moduleConfigs } from "./moduleConfigs";
import { pluginSettings } from "./pluginSettings";
import { resources } from "./resources";
import { schematicNodes } from "./schematicNodes";
import { taskCards } from "./taskCards";
import { taskConfigs } from "./taskConfigs";

export default definePlugin({
  id: "modbus_tcp_client",
  name: "Modbus TCP Client",
  taskCards,
  taskConfigs,
  resources,
  pluginSettings,
  moduleConfigs,
  schematicNodes,
});
