import React from "@dartwic/interface-sdk/react";
import { defineTaskConfig, useTaskConfigBridge } from "@dartwic/interface-sdk/tasks";
import {
  Button, Input, Label, ScrollArea, ScrollBar, Select, SelectContent,
  SelectItem, SelectTrigger, SelectValue,
} from "@dartwic/interface-sdk/ui/general";
import {
  ChannelComboBox,
  convertChannelReferenceToChannelName,
  ModuleInstanceSelect,
} from "@dartwic/interface-sdk/ui/dartwic";
import {
  buildReadWritePayload, hasLegacyTaskArguments, normalizeReadMappings,
  normalizeReadbackInterval, normalizeWriteMappings, readRegisterTypes,
  stableStringify, writeRegisterTypes,
} from "./shared";

function MappingRow({ mapping, registerTypes, channelMode, onChange, onRemove }: any) {
  return (
    <div className="grid items-center gap-2 rounded-md border p-3" style={{ gridTemplateColumns: "180px 120px minmax(0, 1fr) auto" }}>
      <Select value={mapping.registerType} onValueChange={(value: string) => onChange({ ...mapping, registerType: value })}>
        <SelectTrigger><SelectValue /></SelectTrigger>
        <SelectContent>
          {registerTypes.map((type: any) => <SelectItem key={type.value} value={type.value}>{type.label}</SelectItem>)}
        </SelectContent>
      </Select>
      <Input type="number" min="0" value={mapping.register} placeholder="ADDRESS"
        onChange={(event: any) => onChange({ ...mapping, register: event.target.value })} />
      <ChannelComboBox mode={channelMode} showFieldSelector={false} initialValue={mapping.channel}
        placeholder="SELECT FIXED CHANNEL"
        onSelect={(value: string) => onChange({ ...mapping, channel: convertChannelReferenceToChannelName(value) })}
        className="min-w-0 w-full" />
      <Button variant="ghost" onClick={onRemove}>REMOVE</Button>
    </div>
  );
}

function MappingSection({ title, mappings, setMappings, registerTypes, channelMode, idPrefix }: any) {
  const nextId = React.useRef(0);
  return (
    <div className="space-y-2">
      <div className="flex items-center justify-between gap-2">
        <Label>{title}</Label>
        <Button variant="outline" onClick={() => setMappings((current: any[]) => current.concat([{
          id: `${idPrefix}-${nextId.current++}`,
          registerType: registerTypes[0].value,
          register: "",
          channel: "",
        }]))}>ADD</Button>
      </div>
      {mappings.length === 0 ? (
        <div className="rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground">NONE CONFIGURED</div>
      ) : mappings.map((mapping: any, index: number) => (
        <MappingRow key={mapping.id} mapping={mapping} registerTypes={registerTypes} channelMode={channelMode}
          onChange={(next: any) => setMappings((current: any[]) => current.map((item, itemIndex) => itemIndex === index ? next : item))}
          onRemove={() => setMappings((current: any[]) => current.filter((_, itemIndex) => itemIndex !== index))} />
      ))}
    </div>
  );
}

function ModbusTaskConfig({ task, operation, onSaved, onClose, taskEditor }: any) {
  const isReadTask = task.task_type === "modbus_tcp_client.read";
  const [selectedInstance, setSelectedInstance] = React.useState(task.arguments?.module_instance_name || "");
  const [readbackInterval, setReadbackInterval] = React.useState(() => normalizeReadbackInterval(task.arguments));
  const [readMappings, setReadMappings] = React.useState(() => normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName));
  const [writeMappings, setWriteMappings] = React.useState(() => normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName));
  const [errorMessage, setErrorMessage] = React.useState("");
  const [isSaving, setIsSaving] = React.useState(false);
  const legacy = hasLegacyTaskArguments(task.arguments);

  const payload = React.useMemo(() => {
    const combined = buildReadWritePayload(selectedInstance, readbackInterval, readMappings, writeMappings);
    return isReadTask
      ? { module_instance_name: combined.module_instance_name, read_mappings: combined.read_mappings }
      : { module_instance_name: combined.module_instance_name, write_mappings: combined.write_mappings,
          readback_interval_seconds: combined.readback_interval_seconds };
  }, [isReadTask, selectedInstance, readbackInterval, readMappings, writeMappings]);
  const initialPayload = React.useMemo(() => {
    const combined = buildReadWritePayload(task.arguments?.module_instance_name || "", normalizeReadbackInterval(task.arguments),
      normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName),
      normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName));
    return isReadTask
      ? { module_instance_name: combined.module_instance_name, read_mappings: combined.read_mappings }
      : { module_instance_name: combined.module_instance_name, write_mappings: combined.write_mappings,
          readback_interval_seconds: combined.readback_interval_seconds };
  }, [isReadTask, task]);
  const isDirty = stableStringify(payload) !== stableStringify(initialPayload);

  async function saveTask() {
    if (!selectedInstance) return setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
    if (isReadTask ? payload.read_mappings.length === 0 : payload.write_mappings.length === 0) {
      return setErrorMessage(isReadTask ? "ADD AT LEAST ONE READ MAPPING." : "ADD AT LEAST ONE WRITE MAPPING.");
    }
    setIsSaving(true);
    setErrorMessage("");
    try {
      const result = await operation("dartwic/create-task", {
        portal_name: task.portal,
        task_name: task.name,
        task_type: task.task_type,
        arguments: payload,
      }, 30000);
      if (result?.error) return setErrorMessage((result?.payload?.error || "FAILED TO SAVE TASK.").toUpperCase());
      await onSaved?.();
      await onClose?.();
    } finally {
      setIsSaving(false);
    }
  }

  useTaskConfigBridge(taskEditor, {
    isDirty, isSaving, canSave: !legacy, errorMessage,
    saveLabel: "SAVE", cancelLabel: "CANCEL", onSave: saveTask, onCancel: onClose,
  });

  return (
    <div className="flex h-full min-h-0 flex-col gap-4">
      {legacy ? <div className="rounded-md border border-destructive p-3 text-sm text-destructive">
        Legacy Modbus task arguments are unsupported. Create a new Modbus read or write task.
      </div> : null}
      <div className="space-y-2">
        <Label>MODULE CONNECTION</Label>
        <ModuleInstanceSelect
          pluginId="modbus_tcp_client"
          moduleTypeIds={["tcp_client"]}
          value={selectedInstance}
          onValueChange={setSelectedInstance}
          placeholder="SELECT ONE MODBUS CONNECTION"
        />
        <div className="text-xs text-muted-foreground">Each connection allows one read task and one write task.</div>
      </div>
      {!isReadTask ? <div className="space-y-2">
        <Label>READBACK INTERVAL SECONDS</Label>
        <Input type="number" min="0" step="0.1" value={readbackInterval}
          onChange={(event: any) => setReadbackInterval(event.target.value)} />
      </div> : null}
      <ScrollArea className="min-h-0 flex-1" type="always">
        <div className="space-y-6 pr-4">
          {isReadTask ? <MappingSection title="READ MAPPINGS (DEVICE → RAPID)" mappings={readMappings} setMappings={setReadMappings}
            registerTypes={readRegisterTypes} channelMode="write" idPrefix="read" /> : null}
          {!isReadTask ? <MappingSection title="WRITE MAPPINGS (RAPID → DEVICE)" mappings={writeMappings} setMappings={setWriteMappings}
            registerTypes={writeRegisterTypes} channelMode="read" idPrefix="write" /> : null}
        </div>
        <ScrollBar orientation="vertical" />
      </ScrollArea>
    </div>
  );
}

export const taskConfigs = [
  defineTaskConfig({ taskType: "modbus_tcp_client.read", component: ModbusTaskConfig }),
  defineTaskConfig({ taskType: "modbus_tcp_client.write", component: ModbusTaskConfig }),
];
