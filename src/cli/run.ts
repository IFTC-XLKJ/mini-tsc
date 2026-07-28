import { execSync } from "child_process";
import { loadAppJson, type AppJson } from "./app-json.js";

export interface RunOptions {
  cwd: string;
}

export type { AppJson };

export async function run(scriptName: string, options: RunOptions): Promise<void> {
  const appJson = loadAppJson(options.cwd);

  if (!appJson) {
    console.error("Error: app.json not found in current directory");
    console.error("Run 'mini-tsc init <project-name>' to initialize a project first");
    process.exit(1);
  }

  if (!appJson.scripts || !appJson.scripts[scriptName]) {
    console.error(`Error: script '${scriptName}' not found in app.json`);
    console.error(`Available scripts: ${Object.keys(appJson.scripts || {}).join(", ") || "none"}`);
    process.exit(1);
  }

  const command = appJson.scripts[scriptName];
  console.log(`> ${command}`);

  try {
    execSync(command, { cwd: options.cwd, stdio: "inherit" });
  } catch {
    process.exit(1);
  }
}

export async function listScripts(options: RunOptions): Promise<void> {
  const appJson = loadAppJson(options.cwd);

  if (!appJson) {
    console.error("Error: app.json not found in current directory");
    console.error("Run 'mini-tsc init <project-name>' to initialize a project first");
    process.exit(1);
  }

  const scripts = appJson.scripts || {};
  const names = Object.keys(scripts);

  if (names.length === 0) {
    console.log("No scripts defined in app.json");
    return;
  }

  console.log("Available scripts:");
  for (const name of names) {
    console.log(`  ${name}\t${scripts[name]}`);
  }
}
