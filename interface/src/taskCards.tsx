import React from "@dartwic/interface-sdk/react";
import { defineTaskCard } from "@dartwic/interface-sdk/tasks";
import { Separator } from "@dartwic/interface-sdk/ui/general";
import { convertChannelReferenceToChannelName } from "@dartwic/interface-sdk/ui/dartwic";
import { normalizeReadMappings, normalizeWriteMappings } from "./shared";

function ModbusTaskCard({ task }: { task: any }) {
  const reads = normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName);
  const writes = normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName);
  return <>
    <Separator />
    <div className="grid grid-cols-3 gap-2 text-xs">
      <div className="rounded-md border bg-muted/40 px-3 py-2">
        <div className="text-muted-foreground">CONNECTION</div>
        <div className="truncate">{task.arguments?.module_instance_name || "UNBOUND"}</div>
      </div>
      <div className="rounded-md border bg-muted/40 px-3 py-2">
        <div className="text-muted-foreground">READS</div><div>{reads.length}</div>
      </div>
      <div className="rounded-md border bg-muted/40 px-3 py-2">
        <div className="text-muted-foreground">WRITES</div><div>{writes.length}</div>
      </div>
    </div>
    <div className="text-xs text-muted-foreground">Cycle order: write previous commands, then read and commit sensors.</div>
  </>;
}

export const taskCards = [
  defineTaskCard({ taskType: "modbus.read_write", component: ModbusTaskCard }),
];
