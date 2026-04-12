function normalizeMappings(argumentsPayload, convertChannelValuePathToChannelName) {
    if (!argumentsPayload || !Array.isArray(argumentsPayload.mappings)) {
        return [];
    }

    return argumentsPayload.mappings
        .filter((item) => item && typeof item === "object")
        .map((item, index) => ({
            id: `mapping-${index}-${item.register || ""}-${item.channel || ""}`,
            register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
            channel: typeof item.channel === "string"
                ? convertChannelValuePathToChannelName(item.channel)
                : ""
        }));
}

const writeRegisterTypes = [
    { value: "coil", label: "COIL" },
    { value: "holding_register", label: "HOLDING REGISTER" }
];

function normalizeWriteMappings(argumentsPayload, convertChannelValuePathToChannelName) {
    if (argumentsPayload && Array.isArray(argumentsPayload.mappings)) {
        return argumentsPayload.mappings
            .filter((item) => item && typeof item === "object")
            .map((item, index) => ({
                id: `write-mapping-${index}-${item.register || ""}-${item.channel || ""}`,
                registerType: item.register_type === "holding_register" ? "holding_register" : "coil",
                register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
                channel: typeof item.channel === "string"
                    ? convertChannelValuePathToChannelName(item.channel)
                    : ""
            }));
    }

    if (argumentsPayload && (argumentsPayload.register != null || argumentsPayload.channel != null)) {
        return [{
            id: `write-mapping-0-${argumentsPayload.register || ""}-${argumentsPayload.channel || ""}`,
            registerType: argumentsPayload.register_type === "holding_register" ? "holding_register" : "coil",
            register: Number.isFinite(Number(argumentsPayload.register)) ? String(argumentsPayload.register) : "",
            channel: typeof argumentsPayload.channel === "string"
                ? convertChannelValuePathToChannelName(argumentsPayload.channel)
                : ""
        }];
    }

    return [];
}

function normalizeReadbackInterval(argumentsPayload) {
    const value = Number(argumentsPayload?.readback_interval_seconds);
    return Number.isFinite(value) ? String(value) : "0.5";
}

export const moduleUiPluginMeta = {
    moduleName: "modbus_tcp_client",
    taskTypes: ["modbus.read_input_registers", "modbus.write"]
};

export function createModuleUiPlugin(host) {
    const React = host.React;
    const { useEffect, useRef, useState } = React;
    const {
        Button,
        Input,
        Label,
        Select,
        SelectContent,
        SelectItem,
        SelectTrigger,
        SelectValue,
        Separator,
        DialogDescription,
        DialogFooter,
        DialogHeader,
        DialogTitle
    } = host.components;
    const {
        ChannelComboBox,
        convertChannelValuePathToChannelName
    } = host.helpers;

    function ModbusTaskSecondaryGui({ task }) {
        if (task.task_type === "modbus.write") {
            const mappings = normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName);
            const instanceName = task.arguments?.module_instance_name || "UNBOUND";
            const previewMappings = mappings.slice(0, 3);
            const hiddenMappingCount = Math.max(mappings.length - previewMappings.length, 0);
            const mappingPreview = mappings.length === 0
                ? "NO MAPPINGS"
                : mappings.length === 1
                    ? "1 MAPPING"
                    : `${mappings.length} MAPPINGS`;

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
                                {previewMappings.map((mapping) => (
                                    <div
                                        key={mapping.id}
                                        className="truncate rounded-md border bg-muted px-2 py-1"
                                    >
                                        {writeRegisterTypes.find((item) => item.value === mapping.registerType)?.label || "COIL"} {mapping.register} {"->"} {mapping.channel}
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
        const mappings = normalizeMappings(task.arguments, convertChannelValuePathToChannelName);
        const instanceName = task.arguments?.module_instance_name || "UNBOUND";
        const previewMappings = mappings.slice(0, 3);
        const hiddenMappingCount = Math.max(mappings.length - previewMappings.length, 0);
        const mappingPreview = mappings.length === 0
            ? "NO MAPPINGS"
            : mappings.length === 1
                ? "1 MAPPING"
                : `${mappings.length} MAPPINGS`;

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
                            {previewMappings.map((mapping) => (
                                <div
                                    key={mapping.id}
                                    className="truncate rounded-md border bg-muted px-2 py-1"
                                >
                                    {mapping.register} {"->"} {mapping.channel}
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

    function WriteMappingRow({ mapping, onChange, onRemove, removeDisabled }) {
        return (
            <div
                className="grid items-center gap-2 rounded-md border p-3"
                style={{ gridTemplateColumns: "180px 120px minmax(0, 1fr) auto" }}
            >
                <Select
                    value={mapping.registerType}
                    onValueChange={(value) => onChange({ ...mapping, registerType: value })}
                >
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
                <Input
                    type="number"
                    min="0"
                    placeholder="ADDRESS"
                    value={mapping.register}
                    onChange={(event) => onChange({ ...mapping, register: event.target.value })}
                />
                <ChannelComboBox
                    mode="read"
                    showFieldSelector={false}
                    initialValue={mapping.channel}
                    placeholder="SELECT CHANNEL"
                    onSelect={(value) =>
                        onChange({
                            ...mapping,
                            channel: convertChannelValuePathToChannelName(value)
                        })
                    }
                    className="min-w-0 w-full"
                />
                <Button variant="ghost" onClick={onRemove} disabled={removeDisabled}>
                    REMOVE
                </Button>
            </div>
        );
    }

    function ModbusWriteTaskDetailGui({ task, operation, onSaved, onClose }) {
        const mappingIdRef = useRef(0);
        const [moduleInstances, setModuleInstances] = useState([]);
        const [selectedInstance, setSelectedInstance] = useState(task.arguments?.module_instance_name || "");
        const [readbackInterval, setReadbackInterval] = useState(() => normalizeReadbackInterval(task.arguments));
        const [mappings, setMappings] = useState(() =>
            normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
                ...mapping,
                id: `write-mapping-${mappingIdRef.current++}`
            }))
        );
        const [errorMessage, setErrorMessage] = useState("");
        const [isSaving, setIsSaving] = useState(false);

        useEffect(() => {
            setSelectedInstance(task.arguments?.module_instance_name || "");
            setReadbackInterval(normalizeReadbackInterval(task.arguments));
            setMappings(
                normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
                    ...mapping,
                    id: `write-mapping-${mappingIdRef.current++}`
                }))
            );
            setErrorMessage("");
        }, [task]);

        useEffect(() => {
            let active = true;

            async function loadModuleInstances() {
                const result = await operation("dartwic/get-module-instances", {
                    registry_name: moduleUiPluginMeta.moduleName
                }, 15000);

                if (!active) {
                    return;
                }

                if (result?.error) {
                    setModuleInstances([]);
                    return;
                }

                setModuleInstances(result?.payload?.module_instances || []);
            }

            void loadModuleInstances();

            return () => {
                active = false;
            };
        }, [operation]);

        async function saveTask() {
            const cleanedMappings = mappings
                .map((mapping) => ({
                    register_type: mapping.registerType === "holding_register" ? "holding_register" : "coil",
                    register: Number(mapping.register),
                    channel: mapping.channel.trim()
                }))
                .filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== "");

            if (!selectedInstance) {
                setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
                return;
            }

            if (cleanedMappings.length === 0) {
                setErrorMessage("ADD AT LEAST ONE REGISTER/CHANNEL MAPPING.");
                return;
            }

            setIsSaving(true);
            setErrorMessage("");

            try {
                const cleanedReadbackInterval = Number(readbackInterval);
                const result = await operation("dartwic/create-task", {
                    portal_name: task.portal,
                    task_name: task.name,
                    task_type: "modbus.write",
                    arguments: {
                        module_instance_name: selectedInstance,
                        readback_interval_seconds: Number.isFinite(cleanedReadbackInterval) ? cleanedReadbackInterval : 0.5,
                        mappings: cleanedMappings
                    }
                }, 30000);

                if (result?.error) {
                    setErrorMessage((result?.payload?.error || "FAILED TO SAVE TASK.").toUpperCase());
                    return;
                }

                if (onSaved) {
                    await onSaved();
                }

                if (onClose) {
                    onClose();
                }
            } finally {
                setIsSaving(false);
            }
        }

        return (
            <>
                <DialogHeader>
                    <DialogTitle>MODBUS WRITE TASK</DialogTitle>
                    <DialogDescription>
                        BIND THIS TASK TO A MODBUS MODULE INSTANCE AND WRITE DARTWIC CHANNEL VALUES TO COILS OR HOLDING REGISTERS.
                    </DialogDescription>
                </DialogHeader>
                <div className="space-y-4 py-4">
                    <div className="space-y-2">
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
                    <div className="space-y-2">
                        <Label>READBACK INTERVAL SECONDS</Label>
                        <Input
                            type="number"
                            min="0"
                            step="0.1"
                            value={readbackInterval}
                            placeholder="0 disables periodic readback"
                            onChange={(event) => setReadbackInterval(event.target.value)}
                        />
                        <div className="text-xs text-muted-foreground">
                            SET TO 0 TO DISABLE PERIODIC READBACK. SUCCESSFUL WRITES STILL TRIGGER A CONFIRMATION READBACK.
                        </div>
                    </div>
                    <div className="space-y-2">
                        <div className="flex items-center justify-between gap-2">
                            <Label>MAPPINGS</Label>
                            <Button
                                variant="outline"
                                onClick={() =>
                                    setMappings((current) => current.concat([{
                                        id: `write-mapping-${mappingIdRef.current++}`,
                                        registerType: "coil",
                                        register: "",
                                        channel: ""
                                    }]))
                                }
                            >
                                ADD
                            </Button>
                        </div>
                        <div className="max-h-72 space-y-2 overflow-y-auto">
                            {mappings.length === 0 ? (
                                <div className="rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground">
                                    NO MAPPINGS CONFIGURED.
                                </div>
                            ) : (
                                mappings.map((mapping, index) => (
                                    <WriteMappingRow
                                        key={mapping.id}
                                        mapping={mapping}
                                        onChange={(nextMapping) =>
                                            setMappings((current) =>
                                                current.map((item, itemIndex) =>
                                                    itemIndex === index ? nextMapping : item
                                                )
                                            )
                                        }
                                        onRemove={() =>
                                            setMappings((current) =>
                                                current.filter((_, itemIndex) => itemIndex !== index)
                                            )
                                        }
                                        removeDisabled={isSaving}
                                    />
                                ))
                            )}
                        </div>
                    </div>
                    {errorMessage ? (
                        <div className="rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200">
                            {errorMessage}
                        </div>
                    ) : null}
                </div>
                <DialogFooter>
                    <Button variant="ghost" onClick={onClose} disabled={isSaving}>
                        CANCEL
                    </Button>
                    <Button onClick={saveTask} disabled={isSaving}>
                        {isSaving ? "SAVING" : "SAVE"}
                    </Button>
                </DialogFooter>
            </>
        );
    }

    function MappingRow({ mapping, onChange, onRemove, removeDisabled }) {
        return (
            <div
                className="grid items-center gap-2 rounded-md border p-3"
                style={{ gridTemplateColumns: "120px minmax(0, 1fr) auto" }}
            >
                <Input
                    type="number"
                    min="0"
                    placeholder="REGISTER"
                    value={mapping.register}
                    onChange={(event) => onChange({ ...mapping, register: event.target.value })}
                />
                <ChannelComboBox
                    mode="write"
                    showFieldSelector={false}
                    initialValue={mapping.channel}
                    placeholder="SELECT CHANNEL"
                    onSelect={(value) =>
                        onChange({
                            ...mapping,
                            channel: convertChannelValuePathToChannelName(value)
                        })
                    }
                    className="min-w-0 w-full"
                />
                <Button variant="ghost" onClick={onRemove} disabled={removeDisabled}>
                    REMOVE
                </Button>
            </div>
        );
    }

    function ModbusTaskDetailGui({ task, operation, onSaved, onClose }) {
        const mappingIdRef = useRef(0);
        const [moduleInstances, setModuleInstances] = useState([]);
        const [selectedInstance, setSelectedInstance] = useState(task.arguments?.module_instance_name || "");
        const [mappings, setMappings] = useState(() =>
            normalizeMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
                ...mapping,
                id: `mapping-${mappingIdRef.current++}`
            }))
        );
        const [errorMessage, setErrorMessage] = useState("");
        const [isSaving, setIsSaving] = useState(false);

        useEffect(() => {
            setSelectedInstance(task.arguments?.module_instance_name || "");
            setMappings(
                normalizeMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
                    ...mapping,
                    id: `mapping-${mappingIdRef.current++}`
                }))
            );
            setErrorMessage("");
        }, [task]);

        useEffect(() => {
            let active = true;

            async function loadModuleInstances() {
                const result = await operation("dartwic/get-module-instances", {
                    registry_name: moduleUiPluginMeta.moduleName
                }, 15000);

                if (!active) {
                    return;
                }

                if (result?.error) {
                    setModuleInstances([]);
                    return;
                }

                setModuleInstances(result?.payload?.module_instances || []);
            }

            void loadModuleInstances();

            return () => {
                active = false;
            };
        }, [operation]);

        async function saveTask() {
            const cleanedMappings = mappings
                .map((mapping) => ({
                    register: Number(mapping.register),
                    channel: mapping.channel.trim()
                }))
                .filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== "");

            if (!selectedInstance) {
                setErrorMessage("SELECT A MODBUS MODULE INSTANCE.");
                return;
            }

            if (cleanedMappings.length === 0) {
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
                    arguments: {
                        module_instance_name: selectedInstance,
                        mappings: cleanedMappings
                    }
                }, 30000);

                if (result?.error) {
                    setErrorMessage((result?.payload?.error || "FAILED TO SAVE TASK.").toUpperCase());
                    return;
                }

                if (onSaved) {
                    await onSaved();
                }

                if (onClose) {
                    onClose();
                }
            } finally {
                setIsSaving(false);
            }
        }

        return (
            <>
                <DialogHeader>
                    <DialogTitle>MODBUS INPUT REGISTER TASK</DialogTitle>
                    <DialogDescription>
                        BIND THIS TASK TO A MODBUS MODULE INSTANCE AND MAP INPUT REGISTERS TO RAPID CHANNELS.
                    </DialogDescription>
                </DialogHeader>
                <div className="space-y-4 py-4">
                <div className="space-y-2">
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
                    <div className="space-y-2">
                        <div className="flex items-center justify-between gap-2">
                            <Label>MAPPINGS</Label>
                            <Button
                                variant="outline"
                                onClick={() =>
                                    setMappings((current) => current.concat([{
                                        id: `mapping-${mappingIdRef.current++}`,
                                        register: "",
                                        channel: ""
                                    }]))
                                }
                            >
                                ADD
                            </Button>
                        </div>
                        <div className="max-h-72 space-y-2 overflow-y-auto">
                            {mappings.length === 0 ? (
                                <div className="rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground">
                                    NO MAPPINGS CONFIGURED.
                                </div>
                            ) : (
                                mappings.map((mapping, index) => (
                                    <MappingRow
                                        key={mapping.id}
                                        mapping={mapping}
                                        onChange={(nextMapping) =>
                                            setMappings((current) =>
                                                current.map((item, itemIndex) =>
                                                    itemIndex === index ? nextMapping : item
                                                )
                                            )
                                        }
                                        onRemove={() =>
                                            setMappings((current) =>
                                                current.filter((_, itemIndex) => itemIndex !== index)
                                            )
                                        }
                                        removeDisabled={isSaving}
                                    />
                                ))
                            )}
                        </div>
                    </div>
                    {errorMessage ? (
                        <div className="rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200">
                            {errorMessage}
                        </div>
                    ) : null}
                </div>
                <DialogFooter>
                    <Button variant="ghost" onClick={onClose} disabled={isSaving}>
                        CANCEL
                    </Button>
                    <Button onClick={saveTask} disabled={isSaving}>
                        {isSaving ? "SAVING" : "SAVE"}
                    </Button>
                </DialogFooter>
            </>
        );
    }

    function ModbusModuleConfigPage({ instanceConfig, setInstanceConfig, save, moduleConfig = {} }) {
        const [savedParameters, setSavedParameters] = useState(instanceConfig?.parameters || {});
        const [isSaving, setIsSaving] = useState(false);
        const [errorMessage, setErrorMessage] = useState("");
        const parameters = instanceConfig?.parameters || {};
        const isDirty = JSON.stringify(parameters) !== JSON.stringify(savedParameters);

        useEffect(() => {
            setSavedParameters(instanceConfig?.parameters || {});
            setErrorMessage("");
        }, [instanceConfig?.name]);

        function updateParameterField(key, value) {
            setInstanceConfig((prev) => ({
                ...prev,
                parameters: {
                    ...(prev?.parameters || {}),
                    [key]: value
                }
            }));
        }

        async function handleSave() {
            setIsSaving(true);
            setErrorMessage("");

            try {
                await save();
                setSavedParameters(instanceConfig?.parameters || {});
            } catch (error) {
                setErrorMessage((error?.message || String(error)).toUpperCase());
            } finally {
                setIsSaving(false);
            }
        }

        return (
            <div className="flex flex-col gap-4">
                <div className="flex h-fit w-fit flex-row items-center gap-5 rounded-lg border border-border p-2">
                    {(moduleConfig.icon || moduleConfig.icon_image_src) ? (
                        <img className="h-[30px]" src={moduleConfig.icon || moduleConfig.icon_image_src} />
                    ) : null}
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
                            <Input
                                value={parameters.server_ip || ""}
                                placeholder="Enter server IP..."
                                onChange={(event) => updateParameterField("server_ip", event.target.value)}
                            />
                        </div>
                        <div className="flex flex-col gap-1">
                            <Label>Server Port</Label>
                            <Input
                                type="number"
                                value={parameters.server_port == null ? 502 : parameters.server_port}
                                onChange={(event) =>
                                    updateParameterField(
                                        "server_port",
                                        event.target.value === "" ? "" : Number(event.target.value)
                                    )
                                }
                            />
                        </div>
                        <div className="flex flex-col gap-1">
                            <Label>Timeout Seconds</Label>
                            <Input
                                type="number"
                                value={parameters.tv_sec == null ? 3 : parameters.tv_sec}
                                onChange={(event) =>
                                    updateParameterField(
                                        "tv_sec",
                                        event.target.value === "" ? "" : Number(event.target.value)
                                    )
                                }
                            />
                        </div>
                        <div className="flex flex-col gap-1">
                            <Label>Timeout Microseconds</Label>
                            <Input
                                type="number"
                                value={parameters.tv_usec == null ? 0 : parameters.tv_usec}
                                onChange={(event) =>
                                    updateParameterField(
                                        "tv_usec",
                                        event.target.value === "" ? "" : Number(event.target.value)
                                    )
                                }
                            />
                        </div>
                    </div>
                </div>
                {errorMessage ? (
                    <div className="rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200">
                        {errorMessage}
                    </div>
                ) : null}
            </div>
        );
    }

    return {
        id: moduleUiPluginMeta.moduleName,
        moduleName: moduleUiPluginMeta.moduleName,
        taskTypes: moduleUiPluginMeta.taskTypes,
        ModuleConfigPage: ModbusModuleConfigPage,
        TaskSecondaryGui: ModbusTaskSecondaryGui,
        TaskDetailGui: (props) => (
            props.task?.task_type === "modbus.write"
                ? <ModbusWriteTaskDetailGui {...props} />
                : <ModbusTaskDetailGui {...props} />
        )
    };
}



