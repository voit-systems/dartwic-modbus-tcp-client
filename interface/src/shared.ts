export interface MappingRowValue {
  id: string;
  registerType: string;
  register: string;
  channel: string;
}

export const readRegisterTypes = [
  { value: "input_register", label: "INPUT REGISTER" },
  { value: "holding_register", label: "HOLDING REGISTER" },
  { value: "coil", label: "COIL" },
];

export const writeRegisterTypes = [
  { value: "coil", label: "COIL" },
  { value: "holding_register", label: "HOLDING REGISTER" },
];

function normalizeList(argumentsPayload: any, key: string, fallbackType: string,
  convertChannelValuePathToChannelName: (value: string) => string): MappingRowValue[] {
  if (!argumentsPayload || !Array.isArray(argumentsPayload[key])) return [];
  return argumentsPayload[key]
    .filter((item: any) => item && typeof item === "object")
    .map((item: any, index: number) => ({
      id: `${key}-${index}-${item.register ?? ""}-${item.channel ?? ""}`,
      registerType: typeof item.register_type === "string" ? item.register_type : fallbackType,
      register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
      channel: typeof item.channel === "string" ? convertChannelValuePathToChannelName(item.channel) : "",
    }));
}

export function normalizeReadMappings(argumentsPayload: any,
  convertChannelValuePathToChannelName: (value: string) => string) {
  return normalizeList(argumentsPayload, "read_mappings", "input_register", convertChannelValuePathToChannelName);
}

export function normalizeWriteMappings(argumentsPayload: any,
  convertChannelValuePathToChannelName: (value: string) => string) {
  return normalizeList(argumentsPayload, "write_mappings", "coil", convertChannelValuePathToChannelName);
}

export function normalizeReadbackInterval(argumentsPayload: any) {
  const value = Number(argumentsPayload?.readback_interval_seconds);
  return Number.isFinite(value) ? String(value) : "0";
}

function cleanMappings(mappings: MappingRowValue[]) {
  return mappings.map((mapping) => ({
    register_type: mapping.registerType,
    register: Number(mapping.register),
    channel: String(mapping.channel ?? "").trim(),
  })).filter((mapping) => Number.isFinite(mapping.register) && mapping.register >= 0 && mapping.channel !== "");
}

export function buildReadWritePayload(selectedInstance: string, readbackInterval: string,
  readMappings: MappingRowValue[], writeMappings: MappingRowValue[]) {
  const interval = Number(readbackInterval);
  return {
    module_instance_name: selectedInstance,
    read_mappings: cleanMappings(readMappings),
    write_mappings: cleanMappings(writeMappings),
    readback_interval_seconds: Number.isFinite(interval) && interval >= 0 ? interval : 0,
  };
}

export function hasLegacyTaskArguments(argumentsPayload: any) {
  return Boolean(argumentsPayload && Array.isArray(argumentsPayload.mappings));
}

export function stableStringify(value: unknown) {
  return JSON.stringify(value);
}
