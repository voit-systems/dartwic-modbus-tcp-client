export interface MappingRowValue {
  id: string;
  register: string;
  channel: string;
}

export interface WriteMappingRowValue extends MappingRowValue {
  registerType: string;
}

export const writeRegisterTypes = [
  { value: "coil", label: "COIL" },
  { value: "holding_register", label: "HOLDING REGISTER" },
];

export function normalizeMappings(argumentsPayload: any, convertChannelValuePathToChannelName: (value: string) => string): MappingRowValue[] {
  if (!argumentsPayload || !Array.isArray(argumentsPayload.mappings)) {
    return [];
  }

  return argumentsPayload.mappings
    .filter((item: any) => item && typeof item === "object")
    .map((item: any, index: number) => ({
      id: `mapping-${index}-${item.register || ""}-${item.channel || ""}`,
      register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
      channel: typeof item.channel === "string"
        ? convertChannelValuePathToChannelName(item.channel)
        : "",
    }));
}

export function normalizeWriteMappings(argumentsPayload: any, convertChannelValuePathToChannelName: (value: string) => string): WriteMappingRowValue[] {
  if (argumentsPayload && Array.isArray(argumentsPayload.mappings)) {
    return argumentsPayload.mappings
      .filter((item: any) => item && typeof item === "object")
      .map((item: any, index: number) => ({
        id: `write-mapping-${index}-${item.register || ""}-${item.channel || ""}`,
        registerType: item.register_type === "holding_register" ? "holding_register" : "coil",
        register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
        channel: typeof item.channel === "string"
          ? convertChannelValuePathToChannelName(item.channel)
          : "",
      }));
  }

  if (argumentsPayload && (argumentsPayload.register != null || argumentsPayload.channel != null)) {
    return [{
      id: `write-mapping-0-${argumentsPayload.register || ""}-${argumentsPayload.channel || ""}`,
      registerType: argumentsPayload.register_type === "holding_register" ? "holding_register" : "coil",
      register: Number.isFinite(Number(argumentsPayload.register)) ? String(argumentsPayload.register) : "",
      channel: typeof argumentsPayload.channel === "string"
        ? convertChannelValuePathToChannelName(argumentsPayload.channel)
        : "",
    }];
  }

  return [];
}

export function normalizeReadbackInterval(argumentsPayload: any) {
  const value = Number(argumentsPayload?.readback_interval_seconds);
  return Number.isFinite(value) ? String(value) : "0.5";
}

export function buildReadPayload(selectedInstance: string, mappings: MappingRowValue[]) {
  return {
    module_instance_name: selectedInstance,
    mappings: mappings
      .map((mapping) => ({
        register: Number(mapping.register),
        channel: String(mapping.channel ?? "").trim(),
      }))
      .filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== ""),
  };
}

export function buildWritePayload(selectedInstance: string, readbackInterval: string, mappings: WriteMappingRowValue[]) {
  const cleanedReadbackInterval = Number(readbackInterval);
  return {
    module_instance_name: selectedInstance,
    readback_interval_seconds: Number.isFinite(cleanedReadbackInterval) ? cleanedReadbackInterval : 0.5,
    mappings: mappings
      .map((mapping) => ({
        register_type: mapping.registerType === "holding_register" ? "holding_register" : "coil",
        register: Number(mapping.register),
        channel: String(mapping.channel ?? "").trim(),
      }))
      .filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== ""),
  };
}

export function stableStringify(value: unknown) {
  return JSON.stringify(value);
}
