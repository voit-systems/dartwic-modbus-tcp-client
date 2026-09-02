import React from "@dartwic/interface-sdk/react";
import { defineTaskCard } from "@dartwic/interface-sdk/tasks";
import { Separator } from "@dartwic/interface-sdk/ui/general";
import { convertChannelReferenceToChannelName } from "@dartwic/interface-sdk/ui/dartwic";
import { normalizeReadMappings, normalizeWriteMappings } from "./shared";

function ModbusTaskCard({ task }: { task: any }) {
  const isReadTask = task.task_type === "modbus_tcp_client.read";
  const mappingCount = isReadTask
    ? normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName).length
    : normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName).length;
  return <>
    <Separator />
    <div className="grid grid-cols-2 gap-2 text-xs">
      <div className="rounded-md border bg-muted/40 px-3 py-2">
        <div className="text-muted-foreground">CONNECTION</div>
        <div className="truncate">{task.arguments?.module_instance_name || "UNBOUND"}</div>
      </div>
      <div className="rounded-md border bg-muted/40 px-3 py-2">
        <div className="text-muted-foreground">{isReadTask ? "READS" : "WRITES"}</div><div>{mappingCount}</div>
      </div>
    </div>
  </>;
}

export const taskCards = [
  defineTaskCard({ taskType: "modbus_tcp_client.read", component: ModbusTaskCard }),
  defineTaskCard({ taskType: "modbus_tcp_client.write", component: ModbusTaskCard }),
];
