import React from "@dartwic/interface-sdk/react";
import { defineTaskCard } from "@dartwic/interface-sdk/tasks";
import { Separator } from "@dartwic/interface-sdk/ui/general";
import { convertChannelValuePathToChannelName } from "@dartwic/interface-sdk/ui/dartwic";
import { normalizeMappings, normalizeWriteMappings, writeRegisterTypes } from "./shared";

function ModbusTaskCard({ task }: { task: any }) {
  const mappings = task.task_type === "modbus.write"
    ? normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName)
    : normalizeMappings(task.arguments, convertChannelValuePathToChannelName);
  const instanceName = task.arguments?.module_instance_name || "UNBOUND";
  const previewMappings = mappings.slice(0, 3);
  const hiddenMappingCount = Math.max(mappings.length - previewMappings.length, 0);
  const mappingPreview = mappings.length === 0 ? "NO MAPPINGS" : mappings.length === 1 ? "1 MAPPING" : `${mappings.length} MAPPINGS`;

  return (
    <>
      <Separator />
      <div className="grid grid-cols-2 gap-2 text-xs">
        <div className="min-w-0 rounded-md border bg-muted/40 px-3 py-2">
          <div className="text-muted-foreground">MODULE</div>
          <div className="truncate">{instanceName}</div>
        </div>
        <div className="min-w-0 rounded-md border bg-muted/40 px-3 py-2">
          <div className="text-muted-foreground">MAPPINGS</div>
          <div className="truncate">{mappingPreview}</div>
        </div>
      </div>
      <div className="space-y-2">
        <div className="text-[10px] uppercase tracking-wide text-muted-foreground">Mapping Preview</div>
        {previewMappings.length > 0 ? (
          <div className="flex flex-wrap gap-2 text-xs">
            {previewMappings.map((mapping: any) => (
              <div key={mapping.id} className="truncate rounded-md border bg-muted px-2 py-1">
                {"registerType" in mapping
                  ? `${writeRegisterTypes.find((item) => item.value === mapping.registerType)?.label || "COIL"} ${mapping.register} -> ${mapping.channel}`
                  : `${mapping.register} -> ${mapping.channel}`}
              </div>
            ))}
            {hiddenMappingCount > 0 ? (
              <div className="rounded-md border border-dashed px-2 py-1 text-muted-foreground">
                +{hiddenMappingCount} more
              </div>
            ) : null}
          </div>
        ) : (
          <div className="text-xs text-muted-foreground">No mappings configured.</div>
        )}
      </div>
    </>
  );
}

export const taskCards = [
  defineTaskCard({
    taskType: "modbus.write",
    component: ModbusTaskCard,
  }),
  defineTaskCard({
    taskType: "modbus.read_input_registers",
    component: ModbusTaskCard,
  }),
];
