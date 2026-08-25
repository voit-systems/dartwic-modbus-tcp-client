import { definePlugin } from "@dartwic/interface-sdk";
import { moduleConfigs } from "./moduleConfigs";
import { taskCards } from "./taskCards";
import { taskConfigs } from "./taskConfigs";

export default definePlugin({
  id: "modbus",
  name: "Modbus TCP Client",
  register(registry) {
    registry.addTaskUi({
      id: "read_write",
      name: "Modbus Read / Write",
      card: taskCards[0].component,
      editor: taskConfigs[0].component,
    });
    registry.addModuleUi({
      id: "tcp_client",
      name: "Modbus TCP Client",
      panel: moduleConfigs[0].component,
    });
  },
});
