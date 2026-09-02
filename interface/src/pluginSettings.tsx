import React from "@dartwic/interface-sdk/react";
import {Button, Input, Label, Switch} from "@dartwic/interface-sdk/ui/general";

const pluginId = "modbus_tcp_client";

function resultError(result: any, fallback: string) {
  if (!result?.error) return "";
  return String(result?.payload?.error || result?.message || fallback).toUpperCase();
}

function validIpv4Cidr(value: string) {
  const [address, prefixText, ...rest] = value.split("/");
  if (rest.length > 0) return false;
  const octets = address.split(".");
  if (octets.length !== 4 || octets.some((octet) => !/^\d{1,3}$/u.test(octet) || Number(octet) > 255)) {
    return false;
  }
  if (prefixText === undefined) return true;
  const prefix = Number(prefixText);
  return /^\d{1,2}$/u.test(prefixText) && Number.isInteger(prefix) && prefix >= 0 && prefix <= 32;
}

async function installedPluginFolder(operation: any) {
  const result = await operation("dartwic/plugins/list-installed-engine-plugins", {}, 10000);
  const error = resultError(result, "Could not locate the installed engine plugin.");
  if (error) throw new Error(error);
  const plugins = Array.isArray(result?.payload?.plugins) ? result.payload.plugins : [];
  const record = plugins.find((entry: any) => entry?.id === pluginId);
  if (!record?.folder) throw new Error("THE INSTALLED MODBUS ENGINE PLUGIN FOLDER WAS NOT FOUND.");
  return String(record.folder);
}

type NetworkScanSettings = {
  enabled: boolean;
  include_local_subnets: boolean;
  subnets: string[];
  ports: number[];
  unit_ids: number[];
  probe_timeout_ms: number;
  hosts_per_tick: number;
  max_hosts_per_subnet: number;
  minimum_prefix_length: number;
  rescan_interval_seconds: number;
};

async function persistDiscovery(operation: any, startAddress: number, endAddress: number,
  networkScan: NetworkScanSettings) {
  const folder = await installedPluginFolder(operation);
  const read = await operation("dartwic/get-file", {
    rootDir: folder,
    filePath: "plugin.json",
  }, 10000);
  const readError = resultError(read, "Could not read the engine plugin manifest.");
  if (readError) throw new Error(readError);
  const manifest = JSON.parse(String(read?.payload?.content || "{}"));
  const discovery = manifest.device_discovery && typeof manifest.device_discovery === "object"
    ? manifest.device_discovery
    : {};
  const existingTargets = Array.isArray(discovery.targets) ? discovery.targets : [];
  const firstTarget = existingTargets[0] && typeof existingTargets[0] === "object"
    ? existingTargets[0]
    : {host: "127.0.0.1", port: 502, unit_ids: [1, 255]};
  manifest.device_discovery = {
    ...discovery,
    enabled: discovery.enabled !== false,
    network_scan: networkScan,
    targets: [
      {...firstTarget, start_address: startAddress, end_address: endAddress},
      ...existingTargets.slice(1).map((target: any) => ({
        ...target,
        start_address: startAddress,
        end_address: endAddress,
      })),
    ],
  };
  const saved = await operation("dartwic/save-file", {
    rootDir: folder,
    path: "plugin.json",
    content: `${JSON.stringify(manifest, null, 2)}\n`,
  }, 10000);
  const saveError = resultError(saved, "Could not save the discovery settings.");
  if (saveError) throw new Error(saveError);
}

export function ModbusPluginSettings({operation}: any) {
  const [startAddress, setStartAddress] = React.useState("0");
  const [endAddress, setEndAddress] = React.useState("255");
  const [loading, setLoading] = React.useState(true);
  const [saving, setSaving] = React.useState(false);
  const [networkEnabled, setNetworkEnabled] = React.useState(true);
  const [includeLocalSubnets, setIncludeLocalSubnets] = React.useState(true);
  const [subnets, setSubnets] = React.useState("");
  const [ports, setPorts] = React.useState("502");
  const [unitIds, setUnitIds] = React.useState("1, 255");
  const [probeTimeout, setProbeTimeout] = React.useState("50");
  const [maxHosts, setMaxHosts] = React.useState("254");
  const [message, setMessage] = React.useState("");
  const [error, setError] = React.useState("");

  React.useEffect(() => {
    let cancelled = false;
    async function load() {
      try {
        const result = await operation("modbus_tcp_client.get_discovery_settings", {}, 10000);
        const loadError = resultError(result, "Could not load discovery settings.");
        if (loadError) throw new Error(loadError);
        if (!cancelled) {
          setStartAddress(String(result?.payload?.start_address ?? 0));
          setEndAddress(String(result?.payload?.end_address ?? 255));
          const network = result?.payload?.network_scan || {};
          setNetworkEnabled(network.enabled !== false);
          setIncludeLocalSubnets(network.include_local_subnets !== false);
          setSubnets(Array.isArray(network.subnets) ? network.subnets.join(", ") : "");
          setPorts(Array.isArray(network.ports) ? network.ports.join(", ") : "502");
          setUnitIds(Array.isArray(network.unit_ids) ? network.unit_ids.join(", ") : "1, 255");
          setProbeTimeout(String(network.probe_timeout_ms ?? 50));
          setMaxHosts(String(network.max_hosts_per_subnet ?? 254));
        }
      } catch (caught: any) {
        if (!cancelled) setError(String(caught?.message || caught).toUpperCase());
      } finally {
        if (!cancelled) setLoading(false);
      }
    }
    void load();
    return () => { cancelled = true; };
  }, [operation]);

  const start = Number(startAddress);
  const end = Number(endAddress);
  const parseIntegerList = (value: string) => value.split(",").map((entry) => entry.trim())
    .filter(Boolean).map((entry) => Number(entry));
  const parsedPorts = parseIntegerList(ports);
  const parsedUnitIds = parseIntegerList(unitIds);
  const parsedSubnets = subnets.split(",").map((entry) => entry.trim()).filter(Boolean);
  const timeout = Number(probeTimeout);
  const hostLimit = Number(maxHosts);
  const rangeValid = Number.isInteger(start) && Number.isInteger(end) && start >= 0 && end <= 65535 &&
    end >= start && end - start + 1 <= 4096;
  const networkValid = parsedPorts.length > 0 && parsedPorts.every((port) => port >= 1 && port <= 65535) &&
    parsedUnitIds.length > 0 && parsedUnitIds.every((unitId) => unitId >= 0 && (unitId <= 247 || unitId === 255)) &&
    parsedSubnets.every(validIpv4Cidr) &&
    Number.isInteger(timeout) && timeout >= 10 && timeout <= 2000 &&
    Number.isInteger(hostLimit) && hostLimit >= 1 && hostLimit <= 4096;
  const valid = rangeValid && (!networkEnabled || networkValid);

  const networkScan: NetworkScanSettings = {
    enabled: networkEnabled,
    include_local_subnets: includeLocalSubnets,
    subnets: parsedSubnets.every(validIpv4Cidr) ? parsedSubnets : [],
    ports: parsedPorts.length > 0 && parsedPorts.every((port) => Number.isInteger(port) && port >= 1 && port <= 65535)
      ? parsedPorts : [502],
    unit_ids: parsedUnitIds.length > 0 && parsedUnitIds.every((unitId) =>
      Number.isInteger(unitId) && unitId >= 0 && (unitId <= 247 || unitId === 255))
      ? parsedUnitIds : [1, 255],
    probe_timeout_ms: Number.isInteger(timeout) && timeout >= 10 && timeout <= 2000 ? timeout : 50,
    hosts_per_tick: 8,
    max_hosts_per_subnet: Number.isInteger(hostLimit) && hostLimit >= 1 && hostLimit <= 4096 ? hostLimit : 254,
    minimum_prefix_length: 24,
    rescan_interval_seconds: 300,
  };

  async function save() {
    if (!valid) return;
    setSaving(true);
    setError("");
    setMessage("");
    try {
      await persistDiscovery(operation, start, end, networkScan);
      const result = await operation("modbus_tcp_client.configure_discovery", {
        start_address: start,
        end_address: end,
        network_scan: networkScan,
      }, 15000);
      const configureError = resultError(result, "Could not apply the discovery settings.");
      if (configureError) throw new Error(configureError);
      setMessage(`Saved. Discovery will scan responding addresses ${start}–${end}.`);
    } catch (caught: any) {
      setError(String(caught?.message || caught).toUpperCase());
    } finally {
      setSaving(false);
    }
  }

  return (
    <div className="max-w-2xl">
      <div className="border-y border-border/70 py-5">
        <div className="flex flex-wrap items-end justify-between gap-5">
          <div>
            <div className="text-xs font-medium text-foreground">Device discovery range</div>
            <div className="mt-1 text-sm text-muted-foreground">
              The finder probes this inclusive range in all four Modbus address spaces and suggests every responding address.
            </div>
          </div>
          <div className="flex items-end gap-2">
            <div className="w-28 space-y-1">
              <Label htmlFor="modbus-discovery-start">Start</Label>
              <Input id="modbus-discovery-start" type="number" min="0" max="65535"
                value={startAddress} disabled={loading || saving}
                onChange={(event: any) => setStartAddress(event.target.value)} />
            </div>
            <div className="w-28 space-y-1">
              <Label htmlFor="modbus-discovery-end">End</Label>
              <Input id="modbus-discovery-end" type="number" min="0" max="65535"
                value={endAddress} disabled={loading || saving}
                onChange={(event: any) => setEndAddress(event.target.value)} />
            </div>
            <Button variant="outline" disabled={loading || saving || !valid} onClick={save}>
              {saving ? "Saving…" : "Save settings"}
            </Button>
          </div>
        </div>
        {!rangeValid ? (
          <div className="mt-3 text-xs text-destructive">Use an ordered range from 0 to 65535, up to 4096 addresses.</div>
        ) : null}
        {error ? <div className="mt-3 text-xs text-destructive">{error}</div> : null}
        {message ? <div className="mt-3 text-xs text-emerald-300">{message}</div> : null}
      </div>
      <div className="border-b border-border/70 py-5">
        <div className="flex items-center justify-between gap-6">
          <div>
            <div className="text-xs font-medium text-foreground">Network discovery</div>
            <div className="mt-1 text-sm text-muted-foreground">
              Probe local IPv4 networks for Modbus TCP endpoints before scanning their register maps.
            </div>
          </div>
          <Switch checked={networkEnabled} disabled={loading || saving}
            onCheckedChange={(checked: boolean) => setNetworkEnabled(Boolean(checked))} />
        </div>
        <div className="mt-5 flex items-center justify-between gap-6 border-t border-border/50 pt-4">
          <Label htmlFor="modbus-local-subnets" className="normal-case">Scan detected local subnets</Label>
          <Switch id="modbus-local-subnets" checked={includeLocalSubnets} disabled={loading || saving || !networkEnabled}
            onCheckedChange={(checked: boolean) => setIncludeLocalSubnets(Boolean(checked))} />
        </div>
        <div className="mt-4 grid grid-cols-2 gap-4">
          <div className="col-span-2 space-y-1">
            <Label htmlFor="modbus-subnets">Additional IPv4 subnets</Label>
            <Input id="modbus-subnets" value={subnets} disabled={loading || saving || !networkEnabled}
              placeholder="192.168.10.0/24, 10.20.30.0/24"
              onChange={(event: any) => setSubnets(event.target.value)} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="modbus-ports">Ports</Label>
            <Input id="modbus-ports" value={ports} disabled={loading || saving || !networkEnabled}
              placeholder="502" onChange={(event: any) => setPorts(event.target.value)} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="modbus-unit-ids">Unit IDs</Label>
            <Input id="modbus-unit-ids" value={unitIds} disabled={loading || saving || !networkEnabled}
              placeholder="1, 255" onChange={(event: any) => setUnitIds(event.target.value)} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="modbus-probe-timeout">Port timeout (ms)</Label>
            <Input id="modbus-probe-timeout" type="number" min="10" max="2000" value={probeTimeout}
              disabled={loading || saving || !networkEnabled}
              onChange={(event: any) => setProbeTimeout(event.target.value)} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="modbus-max-hosts">Maximum hosts per subnet</Label>
            <Input id="modbus-max-hosts" type="number" min="1" max="4096" value={maxHosts}
              disabled={loading || saving || !networkEnabled}
              onChange={(event: any) => setMaxHosts(event.target.value)} />
          </div>
        </div>
        {networkEnabled && !networkValid ? (
          <div className="mt-3 text-xs text-destructive">Check the ports, unit IDs, timeout, and host limit.</div>
        ) : null}
      </div>
      <div className="pt-4 text-xs text-muted-foreground">
        A Modbus response proves that an address is readable, not that it has a meaningful value. Configure simulators to return ILLEGAL DATA ADDRESS outside their intended map for exact suggestions.
      </div>
    </div>
  );
}
