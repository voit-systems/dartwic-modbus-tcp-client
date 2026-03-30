import path from "node:path";
import { fileURLToPath } from "node:url";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import esbuild from "esbuild";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = __dirname;
const interfaceRoot = path.resolve(repoRoot, "interface");
const packageInfoPath = path.resolve(repoRoot, "package-info.json");
const runtimeEntryPath = path.resolve(interfaceRoot, "src", "runtime-entry.jsx");
const manifestPath = path.resolve(interfaceRoot, "ui", "plugin.json");
const packageInfo = JSON.parse(await readFile(packageInfoPath, "utf8"));
const packageId = packageInfo.id;
const moduleName = packageId;
const releaseUiDir = path.resolve(repoRoot, "package", "interface-module", packageId);
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

console.log(`Built interface plugin for ${moduleName}.`);
for (const target of targets) {
    console.log(`- ${target}`);
}
