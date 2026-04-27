import React from "@dartwic/interface-sdk/react";
import { defineModuleConfig } from "@dartwic/interface-sdk/module-configs";
import { Button, Input, Label } from "@dartwic/interface-sdk/ui/general";

function ModbusModuleConfig({ instanceConfig, setInstanceConfig, save, moduleConfig = {} }: any) {
  const [savedParameters, setSavedParameters] = React.useState(instanceConfig?.parameters || {});
  const [isSaving, setIsSaving] = React.useState(false);
  const [errorMessage, setErrorMessage] = React.useState("");
  const parameters = instanceConfig?.parameters || {};
  const isDirty = JSON.stringify(parameters) !== JSON.stringify(savedParameters);

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

  async function handleSave() {
    setIsSaving(true);
    setErrorMessage("");

    try {
      await save();
      setSavedParameters(instanceConfig?.parameters || {});
    } catch (error: any) {
      setErrorMessage((error?.message || String(error)).toUpperCase());
    } finally {
      setIsSaving(false);
    }
  }

  return (
    <div className="flex flex-col gap-4">
      <div className="flex h-fit w-fit flex-row items-center gap-5 rounded-lg border border-border p-2">
        {(moduleConfig.icon || moduleConfig.icon_image_src) ? <img className="h-[30px]" src={moduleConfig.icon || moduleConfig.icon_image_src} /> : null}
        <Label className="text-lg">{moduleConfig.title || "Modbus TCP Client"}</Label>
      </div>
      <div className="flex h-fit w-fit flex-row justify-center gap-4">
        <div className="flex flex-row items-center gap-5 rounded-lg border border-border p-2">
          <Label>{instanceConfig?.name || ""}</Label>
        </div>
        <Button variant="outline" disabled={!isDirty || isSaving} onClick={handleSave}>
          {isSaving ? "SAVING" : "SAVE CONFIG"}
        </Button>
      </div>
      <div className="flex flex-col gap-3 rounded-lg border border-border p-4">
        <Label className="text-md font-semibold">Connection</Label>
        <div className="flex flex-col gap-4">
          <div className="flex flex-col gap-1">
            <Label>Server IP</Label>
            <Input value={parameters.server_ip || ""} placeholder="Enter server IP..." onChange={(event: any) => updateParameterField("server_ip", event.target.value)} />
          </div>
          <div className="flex flex-col gap-1">
            <Label>Server Port</Label>
            <Input type="number" value={parameters.server_port == null ? 502 : parameters.server_port} onChange={(event: any) => updateParameterField("server_port", event.target.value === "" ? "" : Number(event.target.value))} />
          </div>
          <div className="flex flex-col gap-1">
            <Label>Timeout Seconds</Label>
            <Input type="number" value={parameters.tv_sec == null ? 3 : parameters.tv_sec} onChange={(event: any) => updateParameterField("tv_sec", event.target.value === "" ? "" : Number(event.target.value))} />
          </div>
          <div className="flex flex-col gap-1">
            <Label>Timeout Microseconds</Label>
            <Input type="number" value={parameters.tv_usec == null ? 0 : parameters.tv_usec} onChange={(event: any) => updateParameterField("tv_usec", event.target.value === "" ? "" : Number(event.target.value))} />
          </div>
        </div>
      </div>
      {errorMessage ? (
        <div className="rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200">{errorMessage}</div>
      ) : null}
    </div>
  );
}

export const moduleConfigs = [
  defineModuleConfig({
    component: ModbusModuleConfig,
  }),
];
