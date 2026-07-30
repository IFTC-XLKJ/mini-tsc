import * as fs from "fs";
import * as path from "path";

/** Per-platform icon paths (relative to app.json or absolute). */
export interface AppIconMap {
  /** Windows .ico */
  win?: string;
  windows?: string;
  /** Linux .png / .svg */
  linux?: string;
  /** Fallback used when no platform key matches. */
  default?: string;
}

/** Build-time metadata embedded into the native binary (name, icon, version info, …). */
export interface AppBuildOptions {
  /** Display / product name shown in OS properties (e.g. "My App"). */
  name?: string;
  /** Output binary base name without extension (e.g. "my_app"). Defaults to app name. */
  productName?: string;
  /**
   * Application icon.
   * - string: single path (all platforms)
   * - object: per-platform paths, e.g. `{ "win": "icon.ico", "linux": "icon.png" }`
   */
  icon?: string | AppIconMap;
  /** Short application description (FileDescription / Comment). */
  description?: string;
  /** Author or publisher name. */
  author?: string;
  /** Company / organization (Windows VERSIONINFO CompanyName). */
  company?: string;
  /** Copyright notice. */
  copyright?: string;
  /** Version string for the binary (e.g. "1.0.0"); falls back to app.version. */
  version?: string;
  /**
   * Static assets directory to bundle next to the executable.
   * Relative to app.json or absolute. Copied recursively at build time.
   */
  assetsDir?: string;
  /** Hide the terminal / console window (Windows GUI subsystem). */
  gui?: boolean;
}

export interface AppJson {
  name: string;
  version?: string;
  description?: string;
  author?: string;
  main?: string;
  build?: AppBuildOptions;
  scripts?: Record<string, string>;
}

/** Resolved build metadata ready for the compiler (icon always a single path or absent). */
export interface ResolvedBuildOptions {
  name: string;
  productName: string;
  icon?: string;
  description: string;
  author: string;
  company: string;
  copyright: string;
  version: string;
  assetsDir?: string;
  gui?: boolean;
}

export function loadAppJson(dir: string): AppJson | null {
  const appJsonPath = path.join(dir, "app.json");
  if (!fs.existsSync(appJsonPath)) {
    return null;
  }
  try {
    const content = fs.readFileSync(appJsonPath, "utf-8");
    return JSON.parse(content) as AppJson;
  } catch {
    return null;
  }
}

/** Resolve app.json starting at `startDir`, then walk up a few parents. */
export function findAppJson(startDir: string, maxDepth = 4): { dir: string; app: AppJson } | null {
  let dir = path.resolve(startDir);
  for (let i = 0; i <= maxDepth; i++) {
    const app = loadAppJson(dir);
    if (app) return { dir, app };
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

/**
 * Pick the icon path for the current (or requested) platform.
 * Accepts a plain string or `{ win, linux, darwin, default }`.
 */
export function resolveIconPath(
  icon: string | AppIconMap | undefined,
  platform: string = process.platform,
): string | undefined {
  if (!icon) return undefined;
  if (typeof icon === "string") {
    const s = icon.trim();
    return s.length > 0 ? s : undefined;
  }
  if (typeof icon !== "object" || icon === null) return undefined;

  const isWin = platform === "win32" || platform === "windows";
  const isLinux = platform === "linux" || platform === "android";

  let picked: string | undefined;
  if (isWin) picked = icon.win || icon.windows;
  else if (isLinux) picked = icon.linux;

  if (!picked) picked = icon.default || icon.win || icon.windows || icon.linux;
  if (typeof picked !== "string") return undefined;
  const s = picked.trim();
  return s.length > 0 ? s : undefined;
}

/** Effective build metadata with sensible fallbacks from top-level fields. */
export function resolveBuildOptions(
  app: AppJson,
  platform: string = process.platform,
): ResolvedBuildOptions {
  const b = app.build || {};
  const productName = b.productName || app.name || "output";
  // Resolve assetsDir path
  let assetsDir: string | undefined;
  if (b.assetsDir) {
    assetsDir = b.assetsDir.trim();
  }

  return {
    name: b.name || app.name || productName,
    productName,
    icon: resolveIconPath(b.icon, platform),
    description: b.description || app.description || "",
    author: b.author || app.author || "",
    company: b.company || b.author || app.author || "",
    copyright: b.copyright || "",
    version: b.version || app.version || "1.0.0",
    assetsDir,
    gui: b.gui ?? false,
  };
}

/** Parse "1.2.3" → [1, 2, 3, 0] for Windows FILEVERSION. */
export function parseVersionParts(version: string): [number, number, number, number] {
  const parts = version.replace(/^v/i, "").split(/[.\-]/).map(p => parseInt(p, 10) || 0);
  return [parts[0] || 0, parts[1] || 0, parts[2] || 0, parts[3] || 0];
}
