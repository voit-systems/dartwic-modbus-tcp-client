(() => {
  // interface/src/index.jsx
  function normalizeMappings(argumentsPayload, convertChannelValuePathToChannelName) {
    if (!argumentsPayload || !Array.isArray(argumentsPayload.mappings)) {
      return [];
    }
    return argumentsPayload.mappings.filter((item) => item && typeof item === "object").map((item, index) => ({
      id: `mapping-${index}-${item.register || ""}-${item.channel || ""}`,
      register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
      channel: typeof item.channel === "string" ? convertChannelValuePathToChannelName(item.channel) : ""
    }));
  }
  var writeRegisterTypes = [
    { value: "coil", label: "COIL" },
    { value: "holding_register", label: "HOLDING REGISTER" }
  ];
  function normalizeWriteMappings(argumentsPayload, convertChannelValuePathToChannelName) {
    if (argumentsPayload && Array.isArray(argumentsPayload.mappings)) {
      return argumentsPayload.mappings.filter((item) => item && typeof item === "object").map((item, index) => ({
        id: `write-mapping-${index}-${item.register || ""}-${item.channel || ""}`,
        registerType: item.register_type === "holding_register" ? "holding_register" : "coil",
        register: Number.isFinite(Number(item.register)) ? String(item.register) : "",
        channel: typeof item.channel === "string" ? convertChannelValuePathToChannelName(item.channel) : ""
      }));
    }
    if (argumentsPayload && (argumentsPayload.register != null || argumentsPayload.channel != null)) {
      return [{
        id: `write-mapping-0-${argumentsPayload.register || ""}-${argumentsPayload.channel || ""}`,
        registerType: argumentsPayload.register_type === "holding_register" ? "holding_register" : "coil",
        register: Number.isFinite(Number(argumentsPayload.register)) ? String(argumentsPayload.register) : "",
        channel: typeof argumentsPayload.channel === "string" ? convertChannelValuePathToChannelName(argumentsPayload.channel) : ""
      }];
    }
    return [];
  }
  function normalizeReadbackInterval(argumentsPayload) {
    const value = Number(argumentsPayload?.readback_interval_seconds);
    return Number.isFinite(value) ? String(value) : "0.5";
  }
  var moduleUiPluginMeta = {
    moduleName: "modbus_tcp_client",
    taskTypes: ["modbus.read_input_registers", "modbus.write"]
  };
  function createModuleUiPlugin(host) {
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
        const mappings2 = normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName);
        const instanceName2 = task.arguments?.module_instance_name || "UNBOUND";
        const previewMappings2 = mappings2.slice(0, 3);
        const hiddenMappingCount2 = Math.max(mappings2.length - previewMappings2.length, 0);
        const mappingPreview2 = mappings2.length === 0 ? "NO MAPPINGS" : mappings2.length === 1 ? "1 MAPPING" : `${mappings2.length} MAPPINGS`;
        return /* @__PURE__ */ React.createElement(React.Fragment, null, /* @__PURE__ */ React.createElement(Separator, null), /* @__PURE__ */ React.createElement("div", { className: "grid grid-cols-2 gap-2 text-xs" }, /* @__PURE__ */ React.createElement("div", { className: "min-w-0 rounded-md border bg-muted/40 px-3 py-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-muted-foreground" }, "MODULE"), /* @__PURE__ */ React.createElement("div", { className: "truncate" }, instanceName2)), /* @__PURE__ */ React.createElement("div", { className: "min-w-0 rounded-md border bg-muted/40 px-3 py-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-muted-foreground" }, "MAPPINGS"), /* @__PURE__ */ React.createElement("div", { className: "truncate" }, mappingPreview2))), /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-[10px] uppercase tracking-wide text-muted-foreground" }, "Mapping Preview"), previewMappings2.length > 0 ? /* @__PURE__ */ React.createElement("div", { className: "flex flex-wrap gap-2 text-xs" }, previewMappings2.map((mapping) => /* @__PURE__ */ React.createElement(
          "div",
          {
            key: mapping.id,
            className: "truncate rounded-md border bg-muted px-2 py-1"
          },
          writeRegisterTypes.find((item) => item.value === mapping.registerType)?.label || "COIL",
          " ",
          mapping.register,
          " ",
          "->",
          " ",
          mapping.channel
        )), hiddenMappingCount2 > 0 ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-dashed px-2 py-1 text-muted-foreground" }, "+", hiddenMappingCount2, " more") : null) : /* @__PURE__ */ React.createElement("div", { className: "text-xs text-muted-foreground" }, "No mappings configured.")));
      }
      const mappings = normalizeMappings(task.arguments, convertChannelValuePathToChannelName);
      const instanceName = task.arguments?.module_instance_name || "UNBOUND";
      const previewMappings = mappings.slice(0, 3);
      const hiddenMappingCount = Math.max(mappings.length - previewMappings.length, 0);
      const mappingPreview = mappings.length === 0 ? "NO MAPPINGS" : mappings.length === 1 ? "1 MAPPING" : `${mappings.length} MAPPINGS`;
      return /* @__PURE__ */ React.createElement(React.Fragment, null, /* @__PURE__ */ React.createElement(Separator, null), /* @__PURE__ */ React.createElement("div", { className: "grid grid-cols-2 gap-2 text-xs" }, /* @__PURE__ */ React.createElement("div", { className: "min-w-0 rounded-md border bg-muted/40 px-3 py-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-muted-foreground" }, "MODULE"), /* @__PURE__ */ React.createElement("div", { className: "truncate" }, instanceName)), /* @__PURE__ */ React.createElement("div", { className: "min-w-0 rounded-md border bg-muted/40 px-3 py-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-muted-foreground" }, "MAPPINGS"), /* @__PURE__ */ React.createElement("div", { className: "truncate" }, mappingPreview))), /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement("div", { className: "text-[10px] uppercase tracking-wide text-muted-foreground" }, "Mapping Preview"), previewMappings.length > 0 ? /* @__PURE__ */ React.createElement("div", { className: "flex flex-wrap gap-2 text-xs" }, previewMappings.map((mapping) => /* @__PURE__ */ React.createElement(
        "div",
        {
          key: mapping.id,
          className: "truncate rounded-md border bg-muted px-2 py-1"
        },
        mapping.register,
        " ",
        "->",
        " ",
        mapping.channel
      )), hiddenMappingCount > 0 ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-dashed px-2 py-1 text-muted-foreground" }, "+", hiddenMappingCount, " more") : null) : /* @__PURE__ */ React.createElement("div", { className: "text-xs text-muted-foreground" }, "No mappings configured.")));
    }
    function WriteMappingRow({ mapping, onChange, onRemove, removeDisabled }) {
      return /* @__PURE__ */ React.createElement(
        "div",
        {
          className: "grid items-center gap-2 rounded-md border p-3",
          style: { gridTemplateColumns: "180px 120px minmax(0, 1fr) auto" }
        },
        /* @__PURE__ */ React.createElement(
          Select,
          {
            value: mapping.registerType,
            onValueChange: (value) => onChange({ ...mapping, registerType: value })
          },
          /* @__PURE__ */ React.createElement(SelectTrigger, { className: "w-full" }, /* @__PURE__ */ React.createElement(SelectValue, { placeholder: "REGISTER TYPE" })),
          /* @__PURE__ */ React.createElement(SelectContent, null, writeRegisterTypes.map((registerType) => /* @__PURE__ */ React.createElement(SelectItem, { key: registerType.value, value: registerType.value }, registerType.label)))
        ),
        /* @__PURE__ */ React.createElement(
          Input,
          {
            type: "number",
            min: "0",
            placeholder: "ADDRESS",
            value: mapping.register,
            onChange: (event) => onChange({ ...mapping, register: event.target.value })
          }
        ),
        /* @__PURE__ */ React.createElement(
          ChannelComboBox,
          {
            mode: "read",
            showFieldSelector: false,
            initialValue: mapping.channel,
            placeholder: "SELECT CHANNEL",
            onSelect: (value) => onChange({
              ...mapping,
              channel: convertChannelValuePathToChannelName(value)
            }),
            className: "min-w-0 w-full"
          }
        ),
        /* @__PURE__ */ React.createElement(Button, { variant: "ghost", onClick: onRemove, disabled: removeDisabled }, "REMOVE")
      );
    }
    function ModbusWriteTaskDetailGui({ task, operation, onSaved, onClose }) {
      const mappingIdRef = useRef(0);
      const [moduleInstances, setModuleInstances] = useState([]);
      const [selectedInstance, setSelectedInstance] = useState(task.arguments?.module_instance_name || "");
      const [readbackInterval, setReadbackInterval] = useState(() => normalizeReadbackInterval(task.arguments));
      const [mappings, setMappings] = useState(
        () => normalizeWriteMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
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
          }, 15e3);
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
        const cleanedMappings = mappings.map((mapping) => ({
          register_type: mapping.registerType === "holding_register" ? "holding_register" : "coil",
          register: Number(mapping.register),
          channel: mapping.channel.trim()
        })).filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== "");
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
          }, 3e4);
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
      return /* @__PURE__ */ React.createElement(React.Fragment, null, /* @__PURE__ */ React.createElement(DialogHeader, null, /* @__PURE__ */ React.createElement(DialogTitle, null, "MODBUS WRITE TASK"), /* @__PURE__ */ React.createElement(DialogDescription, null, "BIND THIS TASK TO A MODBUS MODULE INSTANCE AND WRITE DARTWIC CHANNEL VALUES TO COILS OR HOLDING REGISTERS.")), /* @__PURE__ */ React.createElement("div", { className: "space-y-4 py-4" }, /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement(Label, null, "MODULE INSTANCE"), /* @__PURE__ */ React.createElement(Select, { value: selectedInstance, onValueChange: setSelectedInstance }, /* @__PURE__ */ React.createElement(SelectTrigger, { className: "w-full" }, /* @__PURE__ */ React.createElement(SelectValue, { placeholder: "SELECT A MODBUS MODULE INSTANCE" })), /* @__PURE__ */ React.createElement(SelectContent, null, moduleInstances.map((instance) => /* @__PURE__ */ React.createElement(SelectItem, { key: instance.name, value: instance.name }, instance.name))))), /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement(Label, null, "READBACK INTERVAL SECONDS"), /* @__PURE__ */ React.createElement(
        Input,
        {
          type: "number",
          min: "0",
          step: "0.1",
          value: readbackInterval,
          placeholder: "0 disables periodic readback",
          onChange: (event) => setReadbackInterval(event.target.value)
        }
      ), /* @__PURE__ */ React.createElement("div", { className: "text-xs text-muted-foreground" }, "SET TO 0 TO DISABLE PERIODIC READBACK. SUCCESSFUL WRITES STILL TRIGGER A CONFIRMATION READBACK.")), /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement("div", { className: "flex items-center justify-between gap-2" }, /* @__PURE__ */ React.createElement(Label, null, "MAPPINGS"), /* @__PURE__ */ React.createElement(
        Button,
        {
          variant: "outline",
          onClick: () => setMappings((current) => current.concat([{
            id: `write-mapping-${mappingIdRef.current++}`,
            registerType: "coil",
            register: "",
            channel: ""
          }]))
        },
        "ADD"
      )), /* @__PURE__ */ React.createElement("div", { className: "max-h-72 space-y-2 overflow-y-auto" }, mappings.length === 0 ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground" }, "NO MAPPINGS CONFIGURED.") : mappings.map((mapping, index) => /* @__PURE__ */ React.createElement(
        WriteMappingRow,
        {
          key: mapping.id,
          mapping,
          onChange: (nextMapping) => setMappings(
            (current) => current.map(
              (item, itemIndex) => itemIndex === index ? nextMapping : item
            )
          ),
          onRemove: () => setMappings(
            (current) => current.filter((_, itemIndex) => itemIndex !== index)
          ),
          removeDisabled: isSaving
        }
      )))), errorMessage ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200" }, errorMessage) : null), /* @__PURE__ */ React.createElement(DialogFooter, null, /* @__PURE__ */ React.createElement(Button, { variant: "ghost", onClick: onClose, disabled: isSaving }, "CANCEL"), /* @__PURE__ */ React.createElement(Button, { onClick: saveTask, disabled: isSaving }, isSaving ? "SAVING" : "SAVE")));
    }
    function MappingRow({ mapping, onChange, onRemove, removeDisabled }) {
      return /* @__PURE__ */ React.createElement(
        "div",
        {
          className: "grid items-center gap-2 rounded-md border p-3",
          style: { gridTemplateColumns: "120px minmax(0, 1fr) auto" }
        },
        /* @__PURE__ */ React.createElement(
          Input,
          {
            type: "number",
            min: "0",
            placeholder: "REGISTER",
            value: mapping.register,
            onChange: (event) => onChange({ ...mapping, register: event.target.value })
          }
        ),
        /* @__PURE__ */ React.createElement(
          ChannelComboBox,
          {
            mode: "write",
            showFieldSelector: false,
            initialValue: mapping.channel,
            placeholder: "SELECT CHANNEL",
            onSelect: (value) => onChange({
              ...mapping,
              channel: convertChannelValuePathToChannelName(value)
            }),
            className: "min-w-0 w-full"
          }
        ),
        /* @__PURE__ */ React.createElement(Button, { variant: "ghost", onClick: onRemove, disabled: removeDisabled }, "REMOVE")
      );
    }
    function ModbusTaskDetailGui({ task, operation, onSaved, onClose }) {
      const mappingIdRef = useRef(0);
      const [moduleInstances, setModuleInstances] = useState([]);
      const [selectedInstance, setSelectedInstance] = useState(task.arguments?.module_instance_name || "");
      const [mappings, setMappings] = useState(
        () => normalizeMappings(task.arguments, convertChannelValuePathToChannelName).map((mapping) => ({
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
          }, 15e3);
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
        const cleanedMappings = mappings.map((mapping) => ({
          register: Number(mapping.register),
          channel: mapping.channel.trim()
        })).filter((mapping) => Number.isFinite(mapping.register) && mapping.channel !== "");
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
          }, 3e4);
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
      return /* @__PURE__ */ React.createElement(React.Fragment, null, /* @__PURE__ */ React.createElement(DialogHeader, null, /* @__PURE__ */ React.createElement(DialogTitle, null, "MODBUS INPUT REGISTER TASK"), /* @__PURE__ */ React.createElement(DialogDescription, null, "BIND THIS TASK TO A MODBUS MODULE INSTANCE AND MAP INPUT REGISTERS TO RAPID CHANNELS.")), /* @__PURE__ */ React.createElement("div", { className: "space-y-4 py-4" }, /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement(Label, null, "MODULE INSTANCE"), /* @__PURE__ */ React.createElement(Select, { value: selectedInstance, onValueChange: setSelectedInstance }, /* @__PURE__ */ React.createElement(SelectTrigger, { className: "w-full" }, /* @__PURE__ */ React.createElement(SelectValue, { placeholder: "SELECT A MODBUS MODULE INSTANCE" })), /* @__PURE__ */ React.createElement(SelectContent, null, moduleInstances.map((instance) => /* @__PURE__ */ React.createElement(SelectItem, { key: instance.name, value: instance.name }, instance.name))))), /* @__PURE__ */ React.createElement("div", { className: "space-y-2" }, /* @__PURE__ */ React.createElement("div", { className: "flex items-center justify-between gap-2" }, /* @__PURE__ */ React.createElement(Label, null, "MAPPINGS"), /* @__PURE__ */ React.createElement(
        Button,
        {
          variant: "outline",
          onClick: () => setMappings((current) => current.concat([{
            id: `mapping-${mappingIdRef.current++}`,
            register: "",
            channel: ""
          }]))
        },
        "ADD"
      )), /* @__PURE__ */ React.createElement("div", { className: "max-h-72 space-y-2 overflow-y-auto" }, mappings.length === 0 ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-dashed px-3 py-4 text-sm text-muted-foreground" }, "NO MAPPINGS CONFIGURED.") : mappings.map((mapping, index) => /* @__PURE__ */ React.createElement(
        MappingRow,
        {
          key: mapping.id,
          mapping,
          onChange: (nextMapping) => setMappings(
            (current) => current.map(
              (item, itemIndex) => itemIndex === index ? nextMapping : item
            )
          ),
          onRemove: () => setMappings(
            (current) => current.filter((_, itemIndex) => itemIndex !== index)
          ),
          removeDisabled: isSaving
        }
      )))), errorMessage ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200" }, errorMessage) : null), /* @__PURE__ */ React.createElement(DialogFooter, null, /* @__PURE__ */ React.createElement(Button, { variant: "ghost", onClick: onClose, disabled: isSaving }, "CANCEL"), /* @__PURE__ */ React.createElement(Button, { onClick: saveTask, disabled: isSaving }, isSaving ? "SAVING" : "SAVE")));
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
            ...prev?.parameters || {},
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
      return /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-4" }, /* @__PURE__ */ React.createElement("div", { className: "flex h-fit w-fit flex-row items-center gap-5 rounded-lg border border-border p-2" }, moduleConfig.icon || moduleConfig.icon_image_src ? /* @__PURE__ */ React.createElement("img", { className: "h-[30px]", src: moduleConfig.icon || moduleConfig.icon_image_src }) : null, /* @__PURE__ */ React.createElement(Label, { className: "text-lg" }, moduleConfig.title || "Modbus TCP Client")), /* @__PURE__ */ React.createElement("div", { className: "flex h-fit w-fit flex-row justify-center gap-4" }, /* @__PURE__ */ React.createElement("div", { className: "flex flex-row items-center gap-5 rounded-lg border border-border p-2" }, /* @__PURE__ */ React.createElement(Label, null, instanceConfig?.name || "")), /* @__PURE__ */ React.createElement(Button, { variant: "outline", disabled: !isDirty || isSaving, onClick: handleSave }, isSaving ? "SAVING" : "SAVE CONFIG")), /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-3 rounded-lg border border-border p-4" }, /* @__PURE__ */ React.createElement(Label, { className: "text-md font-semibold" }, "Connection"), /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-4" }, /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-1" }, /* @__PURE__ */ React.createElement(Label, null, "Server IP"), /* @__PURE__ */ React.createElement(
        Input,
        {
          value: parameters.server_ip || "",
          placeholder: "Enter server IP...",
          onChange: (event) => updateParameterField("server_ip", event.target.value)
        }
      )), /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-1" }, /* @__PURE__ */ React.createElement(Label, null, "Server Port"), /* @__PURE__ */ React.createElement(
        Input,
        {
          type: "number",
          value: parameters.server_port == null ? 502 : parameters.server_port,
          onChange: (event) => updateParameterField(
            "server_port",
            event.target.value === "" ? "" : Number(event.target.value)
          )
        }
      )), /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-1" }, /* @__PURE__ */ React.createElement(Label, null, "Timeout Seconds"), /* @__PURE__ */ React.createElement(
        Input,
        {
          type: "number",
          value: parameters.tv_sec == null ? 3 : parameters.tv_sec,
          onChange: (event) => updateParameterField(
            "tv_sec",
            event.target.value === "" ? "" : Number(event.target.value)
          )
        }
      )), /* @__PURE__ */ React.createElement("div", { className: "flex flex-col gap-1" }, /* @__PURE__ */ React.createElement(Label, null, "Timeout Microseconds"), /* @__PURE__ */ React.createElement(
        Input,
        {
          type: "number",
          value: parameters.tv_usec == null ? 0 : parameters.tv_usec,
          onChange: (event) => updateParameterField(
            "tv_usec",
            event.target.value === "" ? "" : Number(event.target.value)
          )
        }
      )))), errorMessage ? /* @__PURE__ */ React.createElement("div", { className: "rounded-md border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-200" }, errorMessage) : null);
    }
    return {
      id: moduleUiPluginMeta.moduleName,
      moduleName: moduleUiPluginMeta.moduleName,
      taskTypes: moduleUiPluginMeta.taskTypes,
      ModuleConfigPage: ModbusModuleConfigPage,
      TaskSecondaryGui: ModbusTaskSecondaryGui,
      TaskDetailGui: (props) => props.task?.task_type === "modbus.write" ? /* @__PURE__ */ React.createElement(ModbusWriteTaskDetailGui, { ...props }) : /* @__PURE__ */ React.createElement(ModbusTaskDetailGui, { ...props })
    };
  }

  // interface/src/runtime-entry.jsx
  (function registerModuleUiPlugin() {
    const globalRegistry = window.__dartwicPluginRegistry__ = window.__dartwicPluginRegistry__ || {};
    globalRegistry[moduleUiPluginMeta.moduleName] = {
      createPlugin: createModuleUiPlugin
    };
  })();
})();
