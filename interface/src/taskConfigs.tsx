import React from "@dartwic/interface-sdk/react";
import { defineTaskConfig, useTaskConfigBridge } from "@dartwic/interface-sdk/tasks";
import {
  Button, Input, Label, ScrollArea, ScrollBar, Select, SelectContent,
  SelectItem, SelectTrigger, SelectValue,
} from "@dartwic/interface-sdk/ui/general";
import { ChannelComboBox, convertChannelReferenceToChannelName } from "@dartwic/interface-sdk/ui/dartwic";
import {
  buildReadWritePayload, hasLegacyTaskArguments, normalizeReadMappings,
  normalizeReadbackInterval, normalizeWriteMappings, readRegisterTypes,
  stableStringify, writeRegisterTypes,
} from "./shared";

function useModuleInstances(operation: any) {
  const [instances, setInstances] = React.useState([]);
  React.useEffect(() => {
    let active = true;
    void operation("dartwic/get-module-instances", { registry_name: "modbus" }, 15000).then((result: any) => {
      if (active) setInstances(result?.error ? [] : (result?.payload?.module_instances || []));
    });
    return () => { active = false; };
  }, [operation]);
  return instances;
}

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

function ModbusReadWriteTaskConfig({ task, operation, onSaved, onClose, taskEditor }: any) {
  const moduleInstances = useModuleInstances(operation);
  const [selectedInstance, setSelectedInstance] = React.useState(task.arguments?.module_instance_name || "");
  const [readbackInterval, setReadbackInterval] = React.useState(() => normalizeReadbackInterval(task.arguments));
  const [readMappings, setReadMappings] = React.useState(() => normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName));
  const [writeMappings, setWriteMappings] = React.useState(() => normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName));
  const [errorMessage, setErrorMessage] = React.useState("");
  const [isSaving, setIsSaving] = React.useState(false);
  const legacy = hasLegacyTaskArguments(task.arguments);

  const payload = React.useMemo(() => buildReadWritePayload(
    selectedInstance, readbackInterval, readMappings, writeMappings,
  ), [selectedInstance, readbackInterval, readMappings, writeMappings]);
  const initialPayload = React.useMemo(() => buildReadWritePayload(
    task.arguments?.module_instance_name || "", normalizeReadbackInterval(task.arguments),
    normalizeReadMappings(task.arguments, convertChannelReferenceToChannelName),
    normalizeWriteMappings(task.arguments, convertChannelReferenceToChannelName),
  ), [task]);
  const isDirty = stableStringify(payload) !== stableStringify(initialPayload);

  async function saveTask() {
    if (!selectedInstance) return setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
    if (payload.read_mappings.length === 0 && payload.write_mappings.length === 0) {
      return setErrorMessage("ADD AT LEAST ONE READ OR WRITE MAPPING.");
    }
    setIsSaving(true);
    setErrorMessage("");
    try {
      const result = await operation("dartwic/create-task", {
        portal_name: task.portal,
        task_name: task.name,
        task_type: "modbus.read_write",
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
        Legacy separate Modbus task JSON is unsupported. Create a new modbus.read_write task; it will not run silently.
      </div> : null}
      <div className="space-y-2">
        <Label>MODULE CONNECTION</Label>
        <Select value={selectedInstance} onValueChange={setSelectedInstance}>
          <SelectTrigger><SelectValue placeholder="SELECT ONE CONNECTION" /></SelectTrigger>
          <SelectContent>{moduleInstances.map((instance: any) => (
            <SelectItem key={instance.name} value={instance.name}>{instance.name}</SelectItem>
          ))}</SelectContent>
        </Select>
        <div className="text-xs text-muted-foreground">Only one read/write task may own each connection.</div>
      </div>
      <div className="space-y-2">
        <Label>READBACK INTERVAL SECONDS</Label>
        <Input type="number" min="0" step="0.1" value={readbackInterval}
          onChange={(event: any) => setReadbackInterval(event.target.value)} />
      </div>
      <ScrollArea className="min-h-0 flex-1" type="always">
        <div className="space-y-6 pr-4">
          <MappingSection title="READ MAPPINGS (DEVICE → RAPID)" mappings={readMappings} setMappings={setReadMappings}
            registerTypes={readRegisterTypes} channelMode="write" idPrefix="read" />
          <MappingSection title="WRITE MAPPINGS (RAPID → DEVICE)" mappings={writeMappings} setMappings={setWriteMappings}
            registerTypes={writeRegisterTypes} channelMode="read" idPrefix="write" />
        </div>
        <ScrollBar orientation="vertical" />
      </ScrollArea>
    </div>
  );
}

export const taskConfigs = [
  defineTaskConfig({ taskType: "modbus.read_write", component: ModbusReadWriteTaskConfig }),
];
