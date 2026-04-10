import path from "node:path";
import { fileURLToPath } from "node:url";
import { cp, mkdir, readFile, writeFile } from "node:fs/promises";
import esbuild from "esbuild";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const buildConfigurationPath = path.resolve(__dirname, "build-configuration.json");
const interfaceRoot = path.resolve(__dirname, "interface");
const packageInfoPath = path.resolve(__dirname, "package-info.json");
const runtimeEntryPath = path.resolve(interfaceRoot, "src", "runtime-entry.jsx");
const sourceManifestPath = path.resolve(interfaceRoot, "plugin.json");
const buildConfiguration = JSON.parse(await readFile(buildConfigurationPath, "utf8"));
const packageInfo = JSON.parse(await readFile(packageInfoPath, "utf8"));
const sourceManifest = JSON.parse(await readFile(sourceManifestPath, "utf8"));
const packageId = String(packageInfo.id ?? "").trim();
const pluginId = String(sourceManifest.id ?? "").trim();
const pluginsApiVersion = Number.parseInt(String(sourceManifest.plugins_api_version ?? "").trim(), 10);
const releasePluginDir = path.resolve(__dirname, "package", "interface-plugin", pluginId);
const releaseUiDir = path.resolve(releasePluginDir, "ui");
const releaseManifestPath = path.resolve(releasePluginDir, "plugin.json");
const devInstallPluginDir = path.resolve(__dirname, "..", "interface", "plugins", pluginId);
const shouldInstallDev = process.argv.includes("--install-dev");
const shouldCopyPackage = buildConfiguration.copy_package !== false;

if (!pluginId || String(sourceManifest.type ?? "").trim() !== "interface" || !Number.isInteger(pluginsApiVersion)) {
    throw new Error("interface/plugin.json must include id, type='interface', and integer plugins_api_version.");
}

if (!packageId) {
    throw new Error("package-info.json must include id.");
}

packageInfo.plugins_api_version = pluginsApiVersion;
packageInfo.contains_interface_plugin = true;
await writeFile(packageInfoPath, `${JSON.stringify(packageInfo, null, 2)}\n`, "utf8");

await mkdir(releaseUiDir, { recursive: true });

async function ensureDir(dirPath) {
    await mkdir(dirPath, { recursive: true });
}

async function copyDirectory(sourceDir, targetDir) {
    await ensureDir(path.dirname(targetDir));
    await cp(sourceDir, targetDir, { recursive: true, force: true });
}

const buildResult = await esbuild.build({
    entryPoints: [runtimeEntryPath],
    bundle: true,
    write: false,
    format: "iife",
    platform: "browser",
    jsx: "transform",
    jsxFactory: "React.createElement",
    jsxFragment: "React.Fragment",
    target: "es2020"
});

const runtimeCode = buildResult.outputFiles[0].text;
await cp(sourceManifestPath, releaseManifestPath, { force: true });
await writeFile(path.resolve(releaseUiDir, "index.js"), runtimeCode, "utf8");

const localTargets = shouldInstallDev ? [releasePluginDir, devInstallPluginDir] : [releasePluginDir];
const externalPluginTargets = shouldCopyPackage
    ? [
        buildConfiguration.interface_dir
            ? path.resolve(buildConfiguration.interface_dir, "plugins", pluginId)
            : null,
    ].filter(Boolean)
    : [];

for (const target of localTargets.slice(1)) {
    await copyDirectory(releasePluginDir, target);
}

for (const target of externalPluginTargets) {
    await copyDirectory(releasePluginDir, target);
}

console.log(`Built interface plugin for ${packageId}.`);
for (const target of localTargets) {
    console.log(`- ${target}`);
}
for (const target of externalPluginTargets) {
    console.log(`- copied packaged interface plugin to ${target}`);
}
