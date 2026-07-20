import type { Option } from "./option.js";
import type { Argument } from "./argument.js";

export interface HelpConfig {
  showHidden?: boolean;
  sortSubcommands?: boolean;
}

/**
 * Format help text for a Command.
 * Uses simple loops and Command methods only — Array.filter/map and structural
 * property access on Value-wrapped subcommands crash in the C backend.
 */
export function formatHelp(
  command: {
    getName(): string;
    getDescription(): string;
    getVersion(): string | undefined;
    options: Option[];
    commands: {
      getName(): string;
      getDescription(): string;
      getAlias(): string | undefined;
      _hidden?: boolean;
    }[];
    arguments: Argument[];
  },
  _config?: HelpConfig
): string {
  const lines: string[] = [];

  const usage = command.getName();
  let usageStr = "  Usage: " + usage;

  const cmds = command.commands;
  const cmdCount = cmds ? cmds.length : 0;
  if (cmdCount > 0) {
    usageStr = usageStr + " <command>";
  }

  const opts = command.options;
  const optCount = opts ? opts.length : 0;
  let hasVisibleOpt = false;
  for (let i = 0; i < optCount; i++) {
    const o = opts[i];
    if (o) {
      if (!o.hidden) {
        hasVisibleOpt = true;
      }
    }
  }
  if (hasVisibleOpt) {
    usageStr = usageStr + " [options]";
  }

  const args = command.arguments;
  const argCount = args ? args.length : 0;
  for (let i = 0; i < argCount; i++) {
    const arg = args[i];
    if (!arg) {
      continue;
    }
    let marker = "";
    if (arg.required) {
      marker = "<" + arg.name() + ">";
    } else {
      marker = "[" + arg.name() + "]";
    }
    usageStr = usageStr + " " + marker;
  }
  lines.push(usageStr);

  const desc = command.getDescription();
  if (desc) {
    lines.push("");
    lines.push("  " + desc);
  }

  // Commands section — show all registered subcommands via methods only
  // (do not read _hidden: Value-wrapped Command* would become ts_hashmap_get and crash)
  if (cmdCount > 0) {
    lines.push("");
    lines.push("  Commands:");
    let maxCmdLen = 0;
    for (let i = 0; i < cmdCount; i++) {
      const c = cmds[i];
      if (!c) {
        continue;
      }
      const label = c.getName();
      if (label) {
        if (label.length > maxCmdLen) {
          maxCmdLen = label.length;
        }
      }
    }
    for (let i = 0; i < cmdCount; i++) {
      const c = cmds[i];
      if (!c) {
        continue;
      }
      const label = c.getName();
      const cdesc = c.getDescription();
      let pad = "";
      let padLen = maxCmdLen - (label ? label.length : 0) + 2;
      if (padLen < 0) {
        padLen = 0;
      }
      for (let p = 0; p < padLen; p++) {
        pad = pad + " ";
      }
      const descText = cdesc ? cdesc : "";
      lines.push("    " + label + pad + descText);
    }
  }

  // Options section
  if (hasVisibleOpt) {
    lines.push("");
    lines.push("  Options:");
    let maxOptLen = 0;
    for (let i = 0; i < optCount; i++) {
      const o = opts[i];
      if (!o) {
        continue;
      }
      if (o.hidden) {
        continue;
      }
      const flags = formatOptionFlags(o);
      if (flags.length > maxOptLen) {
        maxOptLen = flags.length;
      }
    }
    for (let i = 0; i < optCount; i++) {
      const opt = opts[i];
      if (!opt) {
        continue;
      }
      if (opt.hidden) {
        continue;
      }
      const flags = formatOptionFlags(opt);
      let pad = "";
      let padLen = maxOptLen - flags.length + 2;
      if (padLen < 0) {
        padLen = 0;
      }
      for (let p = 0; p < padLen; p++) {
        pad = pad + " ";
      }
      const odesc = opt.description ? opt.description : "";
      lines.push("    " + flags + pad + odesc);
    }
  }

  const ver = command.getVersion();
  if (ver) {
    lines.push("");
    lines.push("  " + ver);
  }

  let result = "";
  for (let i = 0; i < lines.length; i++) {
    if (i > 0) {
      result = result + "\n";
    }
    result = result + lines[i];
  }
  // Trailing newline so help output is clean
  result = result + "\n";
  return result;
}

export function formatOptionFlags(opt: Option): string {
  let result = "";
  if (opt.short) {
    result = opt.short;
  }
  if (opt.long) {
    if (result.length > 0) {
      result = result + ", ";
    }
    result = result + opt.long;
  }
  if (opt.required) {
    result = result + " <value>";
  } else if (opt.optional) {
    result = result + " [value]";
  } else if (opt.variadic) {
    result = result + " <value...>";
  }
  return result;
}

export function formatArguments(args: Argument[]): string {
  let result = "";
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!arg) {
      continue;
    }
    if (i > 0) {
      result = result + " ";
    }
    if (arg.variadic) {
      if (arg.required) {
        result = result + "<" + arg.name() + "...>";
      } else {
        result = result + "[" + arg.name() + "...]";
      }
    } else if (arg.required) {
      result = result + "<" + arg.name() + ">";
    } else {
      result = result + "[" + arg.name() + "]";
    }
  }
  return result;
}
