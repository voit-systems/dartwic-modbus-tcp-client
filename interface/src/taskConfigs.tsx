import React from "@dartwic/interface-sdk/react";
import { defineTaskConfig, useTaskConfigBridge } from "@dartwic/interface-sdk/tasks";
import {
  Button,
  Input,
  Label,
  ScrollArea,
  ScrollBar,
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@dartwic/interface-sdk/ui/general";
import {
  ChannelComboBox,
  convertChannelValuePathToChannelName,
} from "@dartwic/interface-sdk/ui/dartwic";
import {
  buildReadPayload,
  buildWritePayload,
  normalizeMappings,
  normalizeReadbackInterval,
  normalizeWriteMappings,
  stableStringify,
  writeRegisterTypes,
} from "./shared";

function WriteMappingRow({ mapping, onChange, onRemove, removeDisabled }: any) {
  return (
    <div className="grid items-center gap-2 rounded-md border p-3" style={{ gridTemplateColumns: "180px 120px minmax(0, 1fr) auto" }}>
      <Select value={mapping.registerType} onValueChange={(value: string) => onChange({ ...mapping, registerType: value })}>
        <SelectTrigger className="w-full">
          <SelectValue placeholder="REGISTER TYPE" />
        </SelectTrigger>
        <SelectContent>
          {writeRegisterTypes.map((registerType) => (
            <SelectItem key={registerType.value} value={registerType.value}>
              {registerType.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
      <Input type="number" min="0" placeholder="ADDRESS" value={mapping.register} onChange={(event: any) => onChange({ ...mapping, register: event.target.value })} />
      <ChannelComboBox
        mode="read"
        showFieldSelector={false}
        initialValue={mapping.channel}
        placeholder="SELECT CHANNEL"
        onSelect={(value: string) => onChange({ ...mapping, channel: convertChannelValuePathToChannelName(value) })}
        className="min-w-0 w-full"
      />
      <Button variant="ghost" onClick={onRemove} disabled={removeDisabled}>REMOVE</Button>
    </div>
  );
}

function MappingRow({ mapping, onChange, onRemove, removeDisabled }: any) {
  return (
    <div className="grid items-center gap-2 rounded-md border p-3" style={{ gridTemplateColumns: "120px minmax(0, 1fr) auto" }}>
      <Input type="number" min="0" placeholder="REGISTER" value={mapping.register} onChange={(event: any) => onChange({ ...mapping, register: event.target.value })} />
      <ChannelComboBox
        mode="write"
        showFieldSelector={false}
        initialValue={mapping.channel}
        placeholder="SELECT CHANNEL"
        onSelect={(value: string) => onChange({ ...mapping, channel: convertChannelValuePathToChannelName(value) })}
        className="min-w-0 w-full"
      />
      <Button variant="ghost" onClick={onRemove} disabled={removeDisabled}>REMOVE</Button>
    </div>
  );
}

function useModuleInstances(operation: any, pluginId: string) {
  const [moduleInstances, setModuleInstances] = React.useState([]);

  React.useEffect(() => {
    let active = true;

    async function loadModuleInstances() {
      const result = await operation("dartwic/get-module-instances", {
        registry_name: pluginId,
      }, 15000);

      if (active) {
        setModuleInstances(result?.error ? [] : (result?.payload?.module_instances || []));
      }
    }

    void loadModuleInstances();
    return () => {
      active = false;
    };
  }, [operation, pluginId]);

  return moduleInstances;
}

function ModbusWriteTaskConfig({ task, operation, onSaved, onClose, taskEditor }: any) {
  const mappingIdRef = React.useRef(0);
  const moduleInstances = useModuleInstances(operation, "modbus_tcp_client");
  const [selectedInstance, setSelectedInstance] = React.useState(task.arguments?.module_instance_name || "");
  const [readbackInterval, setReadbackInterval] = React.useState(() => normalizeReadbackInterval(task.arguments));
  const [mappings, setMappings] = React.useState(() =>
    normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
      ...mapping,
      id: `write-mapping-${mappingIdRef.current++}`,
    }))
  );
  const [errorMessage, setErrorMessage] = React.useState("");
  const [isSaving, setIsSaving] = React.useState(false);

  React.useEffect(() => {
    setSelectedInstance(task.arguments?.module_instance_name || "");
    setReadbackInterval(normalizeReadbackInterval(task.arguments));
    setMappings(
      normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
        ...mapping,
        id: `write-mapping-${mappingIdRef.current++}`,
      }))
    );
    setErrorMessage("");
  }, [task]);

  const currentPayload = React.useMemo(() => buildWritePayload(selectedInstance, readbackInterval, mappings), [mappings, readbackInterval, selectedInstance]);
  const initialPayload = React.useMemo(
    () => buildWritePayload(task.arguments?.module_instance_name || "", normalizeReadbackInterval(task.arguments), normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName)),
    [task]
  );
  const isDirty = stableStringify(currentPayload) !== stableStringify(initialPayload);

  async function saveTask() {
    if (!selectedInstance) {
      setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
      return;
    }

    if (currentPayload.mappings.length === 0) {
      setErrorMessage("ADD AT LEAST ONE REGISTER/CHANNEL MAPPING.");
      return;
    }

    setIsSaving(true);
    setErrorMessage("");

    try {
      const result = await operation("dartwic/create-task", {
        portal_name: task.portal,
        task_name: task.name,
        task_type: "modbus.write",
        arguments: currentPayload,
      }, 30000);

      if (result?.error) {
        setErrorMessage((result?.payload?.error || "FAILED TO SAVE TASK.").toUpperCase());
        return;
      }

      await onSaved?.();
      await onClose?.();
    } finally {
      setIsSaving(false);
    }
  }

  useTaskConfigBridge(taskEditor, {
    isDirty,
    isSaving,
    canSave: true,
    errorMessage,
    saveLabel: "SAVE",
    cancelLabel: "CANCEL",
    onSave: saveTask,
    onCancel: onClose,
  });

  return (
    <div className="flex h-full min-h-0 flex-col gap-4">
      <div className="flex min-h-0 flex-1 flex-col gap-4">
        <div className="shrink-0 space-y-2">
          <Label>MODULE INSTANCE</Label>
          <Select value={selectedInstance} onValueChange={setSelectedInstance}>
            <SelectTrigger className="w-full">
              <SelectValue placeholder="SELECT A MODBUS MODULE INSTANCE" />
            </SelectTrigger>
            <SelectContent>
              {moduleInstances.map((instance) => (
                <SelectItem key={instance.name} value={instance.name}>
                  {instance.name}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="shrink-0 space-y-2">
          <Label>READBACK INTERVAL SECONDS</Label>
          <Input type="number" min="0" step="0.1" value={readbackInterval} placeholder="0 disables periodic readback" onChange={(event: any) => setReadbackInterval(event.target.value)} />
          <div className="text-xs text-muted-foreground">
            Set to 0 to disable periodic readback. Successful writes still trigger a confirmation readback.
          </div>
        </div>
        <div className="flex min-h-0 flex-1 flex-col overflow-hidden">
          <div className="flex items-center justify-between gap-2">
            <Label>MAPPINGS</Label>
            <Button
              variant="outline"
              onClick={() => setMappings((current: any[]) => current.concat([{ id: `write-mapping-${mappingIdRef.current++}`, registerType: "coil", register: "", channel: "" }]))}
            >
              ADD
            </Button>
          </div>
          <div className="mt-2 h-px w-full bg-border" />
          <div className="min-h-0 flex-1 overflow-hidden">
            <ScrollArea className="h-full w-full" type="always">
              <div className="space-y-2 pb-4 pr-4 pt-3">
            {mappings.length === 0 ? (
              <div className="rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground">NO MAPPINGS CONFIGURED.</div>
            ) : (
              mappings.map((mapping, index) => (
                <WriteMappingRow
                  key={mapping.id}
                  mapping={mapping}
                  onChange={(nextMapping: any) => setMappings((current: any[]) => current.map((item, itemIndex) => itemIndex === index ? nextMapping : item))}
                  onRemove={() => setMappings((current: any[]) => current.filter((_, itemIndex) => itemIndex !== index))}
                  removeDisabled={isSaving}
                />
              ))
            )}
              </div>
              <ScrollBar orientation="vertical" />
            </ScrollArea>
          </div>
        </div>
      </div>
    </div>
  );
}

function ModbusReadTaskConfig({ task, operation, onSaved, onClose, taskEditor }: any) {
  const mappingIdRef = React.useRef(0);
  const moduleInstances = useModuleInstances(operation, "modbus_tcp_client");
  const [selectedInstance, setSelectedInstance] = React.useState(task.arguments?.module_instance_name || "");
  const [mappings, setMappings] = React.useState(() =>
    normalizeMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
      ...mapping,
      id: `mapping-${mappingIdRef.current++}`,
    }))
  );
  const [errorMessage, setErrorMessage] = React.useState("");
  const [isSaving, setIsSaving] = React.useState(false);

  React.useEffect(() => {
    setSelectedInstance(task.arguments?.module_instance_name || "");
    setMappings(
      normalizeMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
        ...mapping,
        id: `mapping-${mappingIdRef.current++}`,
      }))
    );
    setErrorMessage("");
  }, [task]);

  const currentPayload = React.useMemo(() => buildReadPayload(selectedInstance, mappings), [mappings, selectedInstance]);
  const initialPayload = React.useMemo(
    () => buildReadPayload(task.arguments?.module_instance_name || "", normalizeMappings(task.arguments, convertChannelValuePathToChannelName)),
    [task]
  );
  const isDirty = stableStringify(currentPayload) !== stableStringify(initialPayload);

  async function saveTask() {
    if (!selectedInstance) {
      setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
      return;
    }

    if (currentPayload.mappings.length === 0) {
      setErrorMessage("ADD AT LEAST ONE REGISTER/CHANNEL MAPPING.");
      return;
    }

    setIsSaving(true);
    setErrorMessage("");

    try {
      const result = await operation("dartwic/create-task", {
        portal_name: task.portal,
        task_name: task.name,
        task_type: task.task_type,
        arguments: currentPayload,
      }, 30000);

      if (result?.error) {
        setErrorMessage((result?.payload?.error || "FAILED TO SAVE TASK.").toUpperCase());
        return;
      }

      await onSaved?.();
      await onClose?.();
    } finally {
      setIsSaving(false);
    }
  }

  useTaskConfigBridge(taskEditor, {
    isDirty,
    isSaving,
    canSave: true,
    errorMessage,
    saveLabel: "SAVE",
    cancelLabel: "CANCEL",
    onSave: saveTask,
    onCancel: onClose,
  });

  return (
    <div className="flex h-full min-h-0 flex-col gap-4">
      <div className="flex min-h-0 flex-1 flex-col gap-4">
        <div className="shrink-0 space-y-2">
          <Label>MODULE INSTANCE</Label>
          <Select value={selectedInstance} onValueChange={setSelectedInstance}>
            <SelectTrigger className="w-full">
              <SelectValue placeholder="SELECT A MODBUS MODULE INSTANCE" />
            </SelectTrigger>
            <SelectContent>
              {moduleInstances.map((instance) => (
                <SelectItem key={instance.name} value={instance.name}>
                  {instance.name}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="flex min-h-0 flex-1 flex-col overflow-hidden">
          <div className="flex items-center justify-between gap-2">
            <Label>MAPPINGS</Label>
            <Button
              variant="outline"
              onClick={() => setMappings((current: any[]) => current.concat([{ id: `mapping-${mappingIdRef.current++}`, register: "", channel: "" }]))}
            >
              ADD
            </Button>
          </div>
          <div className="mt-2 h-px w-full bg-border" />
          <div className="min-h-0 flex-1 overflow-hidden">
            <ScrollArea className="h-full w-full" type="always">
              <div className="space-y-2 pb-4 pr-4 pt-3">
            {mappings.length === 0 ? (
              <div className="rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground">NO MAPPINGS CONFIGURED.</div>
            ) : (
              mappings.map((mapping, index) => (
                <MappingRow
                  key={mapping.id}
                  mapping={mapping}
                  onChange={(nextMapping: any) => setMappings((current: any[]) => current.map((item, itemIndex) => itemIndex === index ? nextMapping : item))}
                  onRemove={() => setMappings((current: any[]) => current.filter((_, itemIndex) => itemIndex !== index))}
                  removeDisabled={isSaving}
                />
              ))
            )}
              </div>
              <ScrollBar orientation="vertical" />
            </ScrollArea>
          </div>
        </div>
      </div>
    </div>
  );
}

export const taskConfigs = [
  defineTaskConfig({
    taskType: "modbus.write",
    component: ModbusWriteTaskConfig,
  }),
  defineTaskConfig({
    taskType: "modbus.read_input_registers",
    component: ModbusReadTaskConfig,
  }),
];
