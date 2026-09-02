import React from "@dartwic/interface-sdk/react";
import { defineModuleConfig, useModuleConfigBridge } from "@dartwic/interface-sdk/module-configs";
import { Input, Label } from "@dartwic/interface-sdk/ui/general";
import { ModuleRuntimeOverview } from "@dartwic/interface-sdk/ui/dartwic";

function mappingDetail(mapping: any) {
  const registerType = String(mapping?.register_type || "register").replace(/_/g, " ").toUpperCase();
  const register = Number(mapping?.register);
  return Number.isFinite(register) ? `${registerType} ${register}` : registerType;
}

function resolveModbusTaskChannels(task: any) {
  const reads = Array.isArray(task?.arguments?.read_mappings) ? task.arguments.read_mappings : [];
  const writes = Array.isArray(task?.arguments?.write_mappings) ? task.arguments.write_mappings : [];
  return [
    ...reads.map((mapping: any) => ({
      name: String(mapping?.channel ?? "").trim(),
      direction: "input" as const,
      detail: mappingDetail(mapping),
    })),
    ...writes.map((mapping: any) => ({
      name: String(mapping?.channel ?? "").trim(),
      direction: "output" as const,
      detail: mappingDetail(mapping),
    })),
  ].filter((channel) => channel.name);
}

function ConnectionField({label, htmlFor, children}: {label: string; htmlFor: string; children?: any}) {
  return (
    <div className="min-w-0 space-y-1.5">
      <Label htmlFor={htmlFor} className="block text-[11px] text-muted-foreground">{label}</Label>
      {children}
    </div>
  );
}

function ConnectionFieldRow({label, children}: {label: string; children?: any}) {
  return (
    <div
      className="grid min-h-12 items-end gap-5 border-t border-border/70 px-3 py-3 last:border-b"
      style={{gridTemplateColumns: "minmax(140px, 0.45fr) minmax(0, 1.55fr)"}}
    >
      <div className="pb-2 text-xs text-muted-foreground">{label}</div>
      {children}
    </div>
  );
}

function ModbusModuleConfig({ instanceConfig, setInstanceConfig, save, moduleEditor }: any) {
  const [savedParameters, setSavedParameters] = React.useState(instanceConfig?.parameters || {});
  const [isSaving, setIsSaving] = React.useState(false);
  const [errorMessage, setErrorMessage] = React.useState("");
  const parameters = instanceConfig?.parameters || {};
  const isDirty = JSON.stringify(parameters) !== JSON.stringify(savedParameters);
  const saveRef = React.useRef(save);
  const parametersRef = React.useRef(parameters);
  saveRef.current = save;
  parametersRef.current = parameters;

  React.useEffect(() => {
    setSavedParameters(instanceConfig?.parameters || {});
    setErrorMessage("");
  }, [instanceConfig?.name]);

  function updateParameterField(key: string, value: unknown) {
    setInstanceConfig((prev: any) => ({
      ...prev,
      parameters: {
        ...(prev?.parameters || {}),
        [key]: value,
      },
    }));
  }

  const handleSave = React.useCallback(async () => {
    setIsSaving(true);
    setErrorMessage("");

    try {
      await saveRef.current();
      setSavedParameters(parametersRef.current);
    } catch (error: any) {
      setErrorMessage((error?.message || String(error)).toUpperCase());
    } finally {
      setIsSaving(false);
    }
  }, []);

  useModuleConfigBridge(moduleEditor, {
    isDirty,
    isSaving,
    canSave: true,
    errorMessage,
    saveLabel: "SAVE CONFIG",
    onSave: handleSave,
  });

  return (
    <div className="space-y-8">
      <div className="pb-2">
        <div className="mb-3 text-xs font-medium text-muted-foreground">CONNECTION DETAILS</div>
        <div>
          <ConnectionFieldRow label="Endpoint">
            <div className="grid min-w-0 gap-3" style={{gridTemplateColumns: "minmax(180px, 1fr) minmax(90px, 0.4fr) minmax(80px, 0.35fr)"}}>
              <ConnectionField label="Address" htmlFor="modbus-server-ip">
                <Input id="modbus-server-ip" value={parameters.server_ip || ""} placeholder="127.0.0.1" onChange={(event: any) => updateParameterField("server_ip", event.target.value)} />
              </ConnectionField>
              <ConnectionField label="Port" htmlFor="modbus-server-port">
                <Input id="modbus-server-port" type="number" value={parameters.server_port == null ? 502 : parameters.server_port} onChange={(event: any) => updateParameterField("server_port", event.target.value === "" ? "" : Number(event.target.value))} />
              </ConnectionField>
              <ConnectionField label="Unit ID" htmlFor="modbus-unit-id">
                <Input id="modbus-unit-id" type="number" min="0" max="255" value={parameters.unit_id == null ? 255 : parameters.unit_id} onChange={(event: any) => updateParameterField("unit_id", event.target.value === "" ? "" : Number(event.target.value))} />
              </ConnectionField>
            </div>
          </ConnectionFieldRow>
          <ConnectionFieldRow label="Response timeout">
            <div className="grid min-w-0 grid-cols-2 gap-3">
              <ConnectionField label="Seconds" htmlFor="modbus-timeout-seconds">
                <Input id="modbus-timeout-seconds" type="number" min="0" value={parameters.tv_sec == null ? 3 : parameters.tv_sec} onChange={(event: any) => updateParameterField("tv_sec", event.target.value === "" ? "" : Number(event.target.value))} />
              </ConnectionField>
              <ConnectionField label="Microseconds" htmlFor="modbus-timeout-microseconds">
                <Input id="modbus-timeout-microseconds" type="number" min="0" max="999999" value={parameters.tv_usec == null ? 0 : parameters.tv_usec} onChange={(event: any) => updateParameterField("tv_usec", event.target.value === "" ? "" : Number(event.target.value))} />
              </ConnectionField>
            </div>
          </ConnectionFieldRow>
          <ConnectionFieldRow label="Event attribution">
            <div className="grid min-w-0 grid-cols-2 gap-3">
              <ConnectionField label="System" htmlFor="modbus-event-system">
                <Input id="modbus-event-system" value={parameters.event_system || ""} placeholder="Software" onChange={(event: any) => updateParameterField("event_system", event.target.value)} />
              </ConnectionField>
              <ConnectionField label="Subsystem" htmlFor="modbus-event-subsystem">
                <Input id="modbus-event-subsystem" value={parameters.event_subsystem || ""} placeholder="Modbus" onChange={(event: any) => updateParameterField("event_subsystem", event.target.value)} />
              </ConnectionField>
            </div>
          </ConnectionFieldRow>
        </div>
      </div>
      <ModuleRuntimeOverview
        instanceName={instanceConfig?.name || ""}
        resolveTaskChannels={resolveModbusTaskChannels}
        emptyMessage="NO TASKS LINKED TO THIS MODULE"
        className="pt-2"
      />
    </div>
  );
}

export const moduleConfigs = [
  defineModuleConfig({
    component: ModbusModuleConfig,
  }),
];
