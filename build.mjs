import path from "node:path";
import { fileURLToPath } from "node:url";
import { cp, mkdir, readFile, writeFile } from "node:fs/promises";
import esbuild from "esbuild";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = __dirname;
const interfaceRoot = path.resolve(repoRoot, "interface");
const packageInfoPath = path.resolve(repoRoot, "package-info.json");
const buildConfigurationPath = path.resolve(repoRoot, "build-configuration.json");
const runtimeEntryPath = path.resolve(interfaceRoot, "src", "runtime-entry.jsx");
const manifestPath = path.resolve(interfaceRoot, "ui", "plugin.json");
const packageInfo = JSON.parse(await readFile(packageInfoPath, "utf8"));
const buildConfiguration = JSON.parse(await readFile(buildConfigurationPath, "utf8"));
const packageId = packageInfo.id;
const moduleName = packageId;
const releaseModuleDir = path.resolve(repoRoot, "package", "interface-module", packageId);
const releaseUiDir = path.resolve(releaseModuleDir, "ui");
const devInstallUiDir = path.resolve(repoRoot, "interface", "modules", "registry", moduleName, "ui");
const shouldInstallDev = process.argv.includes("--install-dev");

async function ensureDir(dirPath) {
    await mkdir(dirPath, { recursive: true });
}

async function writeRuntimeBundle(targetDir, runtimeCode, manifestText) {
    await ensureDir(targetDir);
    await writeFile(path.resolve(targetDir, "index.js"), runtimeCode, "utf8");
    await writeFile(path.resolve(targetDir, "plugin.json"), manifestText, "utf8");
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
const manifestText = await readFile(manifestPath, "utf8");
const targets = shouldInstallDev ? [releaseUiDir, devInstallUiDir] : [releaseUiDir];

for (const target of targets) {
    await writeRuntimeBundle(target, runtimeCode, manifestText);
}

const externalRegistryTargets = [
    buildConfiguration.interface_dir
        ? path.resolve(buildConfiguration.interface_dir, "modules", "registry", moduleName)
        : null
].filter(Boolean);

for (const target of externalRegistryTargets) {
    await copyDirectory(releaseModuleDir, target);
}

console.log(`Built interface plugin for ${moduleName}.`);
for (const target of targets) {
    console.log(`- ${target}`);
}
for (const target of externalRegistryTargets) {
    console.log(`- copied packaged interface module to ${target}`);
}
