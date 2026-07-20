import { Option, camelcase } from "./option.js";
import { Argument } from "./argument.js";
import { formatHelp } from "./help.js";
import { CommanderError } from "./error.js";

declare const process: {
  argv: string[];
  stdout: { write(str: string): boolean };
  stderr: { write(str: string): boolean };
  exit(code: number): never;
};

export type ActionHandler = (...args: any[]) => void | Promise<void>;

export interface CommandConfig {
  isDefault?: boolean;
  hidden?: boolean;
}

interface OutputConfig {
  writeOut: (str: string) => void;
  writeErr: (str: string) => void;
}

export class Command {
  private _name: string = "";
  private _description: string = "";
  private _version?: string;
  private _versionFlags: string = "-v, --version";
  private _versionDescription: string = "output the version number";
  private _parent: Command | null = null;
  private _exitOverride: boolean = false;
  private _outputConfig: OutputConfig = {
    writeOut: (str) => process.stdout.write(str),
    writeErr: (str) => process.stderr.write(str),
  };
  private _helpEnabled: boolean = true;

  options: Option[] = [];
  commands: Command[] = [];
  arguments: Argument[] = [];
  private _action?: ActionHandler;
  private _aliases: string[] = [];
  private _hidden: boolean = false;
  private _defaultCommand: boolean = false;
  private _allowUnknownOption: boolean = false;
  private _allowExcessArguments: boolean = true;
  private _preActionHooks: ActionHandler[] = [];
  private _postActionHooks: ActionHandler[] = [];

  constructor(name?: string, config?: CommandConfig) {
    // Explicit array inits (class field `= []` is also emitted; keep both safe).
    this.options = [];
    this.commands = [];
    this.arguments = [];
    this._aliases = [];
    this._preActionHooks = [];
    this._postActionHooks = [];
    this.args = [];
    this.processedArgs = [];

    if (name) {
      this._name = name;
    }
    // Avoid config?. — optional chaining on undefined crashes the C backend.
    // config defaults are unused for the common `new Command()` / `new Command(name)` paths.
    if (config) {
      if (config.isDefault) {
        this._defaultCommand = true;
      }
      if (config.hidden) {
        this._hidden = true;
      }
    }
  }

  name(name?: string): this {
    // Use falsy check, not === undefined — C backend mishandles the latter on pointers.
    if (!name) return this;
    this._name = name;
    return this;
  }

  /** Returns the name string (non-chainable). */
  getName(): string {
    return this._name;
  }

  description(desc?: string): this {
    if (!desc) return this;
    this._description = desc;
    return this;
  }

  /** Returns the description string (non-chainable). */
  getDescription(): string {
    return this._description;
  }

  version(ver: string, flags: string = "-v, --version", desc: string = "output the version number"): this {
    this._version = ver;
    this._versionFlags = flags;
    this._versionDescription = desc;
    return this;
  }

  /**
   * If any arg is a version flag, print version and exit.
   * Separate ifs only — no `||` chains (C backend wraps those as ts_to_boolean(int)).
   */
  private _maybePrintVersion(args: string[]): void {
    if (!this._version) return;
    for (let i = 0; i < args.length; i++) {
      const arg = args[i];
      if (arg === "-v") {
        process.stdout.write(this._version + "\n");
        this._exit(0, "commander.version", this._version);
      }
      if (arg === "-V") {
        process.stdout.write(this._version + "\n");
        this._exit(0, "commander.version", this._version);
      }
      if (arg === "--version") {
        process.stdout.write(this._version + "\n");
        this._exit(0, "commander.version", this._version);
      }
    }
  }

  /** Returns the version string (non-chainable). */
  getVersion(): string | undefined {
    return this._version;
  }

  command(nameAndArgs: string, config: CommandConfig = {}): Command {
    // Match commander.js: "test <arg> [opt]" → name "test" + registered arguments.
    // Separate ifs only — no `||` chains (C backend wraps those as ts_to_boolean(int)).
    let cmdName = "";
    let i = 0;
    while (i < nameAndArgs.length) {
      const c = nameAndArgs.charAt(i);
      if (c === " ") {
        i++;
        continue;
      }
      if (c === "\t") {
        i++;
        continue;
      }
      break;
    }
    while (i < nameAndArgs.length) {
      const c = nameAndArgs.charAt(i);
      if (c === " ") break;
      if (c === "\t") break;
      if (c === "<") break;
      if (c === "[") break;
      cmdName = cmdName + c;
      i++;
    }
    const cmd = new Command(cmdName);
    cmd._parent = this;
    if (config) {
      if (config.hidden) {
        cmd._hidden = true;
      }
    }

    while (i < nameAndArgs.length) {
      const c = nameAndArgs.charAt(i);
      if (c === " ") {
        i++;
        continue;
      }
      if (c === "\t") {
        i++;
        continue;
      }
      if (c === "<") {
        let token = "";
        i++;
        while (i < nameAndArgs.length) {
          const ch = nameAndArgs.charAt(i);
          if (ch === ">") {
            i++;
            break;
          }
          token = token + ch;
          i++;
        }
        cmd.argument("<" + token + ">");
        continue;
      }
      if (c === "[") {
        let token = "";
        i++;
        while (i < nameAndArgs.length) {
          const ch = nameAndArgs.charAt(i);
          if (ch === "]") {
            i++;
            break;
          }
          token = token + ch;
          i++;
        }
        cmd.argument("[" + token + "]");
        continue;
      }
      // skip unexpected token
      while (i < nameAndArgs.length) {
        const ch = nameAndArgs.charAt(i);
        if (ch === " ") break;
        if (ch === "\t") break;
        if (ch === "<") break;
        if (ch === "[") break;
        i++;
      }
    }

    this.commands.push(cmd);
    return cmd;
  }

  alias(alias?: string): this {
    if (!alias) return this;
    this._aliases.push(alias);
    return this;
  }

  /** Returns the first alias (non-chainable). */
  getAlias(): string | undefined {
    return this._aliases[0];
  }

  argument(name: string, description?: string, defaultValue?: unknown): this {
    const arg = new Argument(name, description || "");
    if (defaultValue !== undefined) {
      arg.default(defaultValue);
    }
    this.arguments.push(arg);
    return this;
  }

  option(flags: string, description?: string, defaultValue?: unknown): this {
    const opt = new Option(flags, description || "");
    if (defaultValue !== undefined) {
      opt.default(defaultValue);
    }
    this.options.push(opt);
    return this;
  }

  requiredOption(flags: string, description?: string, defaultValue?: unknown): this {
    const opt = new Option(flags, description || "");
    if (defaultValue !== undefined) {
      opt.default(defaultValue);
    }
    opt.makeOptionMandatory();
    this.options.push(opt);
    return this;
  }

  addOption(opt: Option): this {
    this.options.push(opt);
    return this;
  }

  addArgument(arg: Argument): this {
    this.arguments.push(arg);
    return this;
  }

  addCommand(cmd: Command): this {
    this.commands.push(cmd);
    cmd._parent = this;
    return this;
  }

  action(fn: ActionHandler): this {
    this._action = fn;
    return this;
  }

  allowUnknownOption(allow: boolean = true): this {
    this._allowUnknownOption = allow;
    return this;
  }

  allowExcessArguments(allow: boolean = true): this {
    this._allowExcessArguments = allow;
    return this;
  }

  hook(event: "preAction" | "postAction", fn: ActionHandler): this {
    if (event === "preAction") this._preActionHooks.push(fn);
    if (event === "postAction") this._postActionHooks.push(fn);
    return this;
  }

  exitOverride(): this {
    this._exitOverride = true;
    return this;
  }

  configureOutput(config: Partial<OutputConfig>): this {
    if (config.writeOut) this._outputConfig.writeOut = config.writeOut;
    if (config.writeErr) this._outputConfig.writeErr = config.writeErr;
    return this;
  }

  hidden(): this {
    this._hidden = true;
    return this;
  }

  // ---- Getters ----

  get parent(): Command | null {
    return this._parent;
  }

  get hiddenCommands(): boolean {
    return this._hidden;
  }

  get isDefaultCommand(): boolean {
    return this._defaultCommand;
  }

  // ---- Help ----

  help(_context?: { error?: boolean }): never {
    // Always write help to stdout. Avoid context?. and function-pointer
    // writeOut (Value vs TSString* ABI mismatch in the C backend).
    const str = this.helpInformation();
    process.stdout.write(str);
    this._exit(0, "commander.help", "(outputHelp)");
  }

  outputHelp(): void {
    const str = this.helpInformation();
    process.stdout.write(str);
  }

  helpInformation(): string {
    return formatHelp(this as any);
  }

  // ---- Parse ----

  parse(argv?: string[], options?: { from?: "node" | "electron" | "user" }): this {
    const args = this._prepareArgs(argv, options);
    this._parseArgs(args);
    return this;
  }

  async parseAsync(argv?: string[], options?: { from?: "node" | "electron" | "user" }): Promise<this> {
    const args = this._prepareArgs(argv, options);
    await this._parseArgsAsync(args);
    return this;
  }

  // ---- Parsed results ----

  args: string[] = [];
  processedArgs: string[] = [];

  opts<T extends Record<string, unknown> = Record<string, unknown>>(): T {
    return this._opts as T;
  }

  private _opts: Record<string, unknown> = {};

  // ---- Internal ----

  private _prepareArgs(argv?: string[], _options?: { from?: "node" | "electron" | "user" }): string[] {
    let args: string[];
    if (argv) {
      args = [...argv];
    } else {
      args = process.argv.slice(0);
    }
    // Always auto-detect. Reading optional `options.from` crashes the C
    // backend when options is undefined (null .as.object deref).
    const from = this._detectArgvFrom(args);
    if (from === "node" || from === "electron") {
      args = args.slice(2);
    } else {
      // "user" mode: only skip 1 (the program name) — native binaries
      args = args.slice(1);
    }
    return args;
  }

  /** Infer parse mode: node/electron (2 prefix args) vs native user (1). */
  private _detectArgvFrom(args: string[]): "node" | "electron" | "user" {
    if (args.length === 0) {
      return "user";
    }
    // Prefer endsWith/=== only — lastIndexOf/|| chains break the TS→C emitter.
    const first = args[0];
    if (first === "node") return "node";
    if (first.endsWith("node.exe")) return "node";
    if (first.endsWith("/node")) return "node";
    if (first.endsWith("\\node")) return "node";
    if (first === "electron") return "electron";
    if (first.endsWith("electron.exe")) return "electron";
    if (first.endsWith("/electron")) return "electron";
    return "user";
  }

  private async _parseArgsAsync(args: string[]): Promise<void> {
    // Find matching command first
    const cmdName = args[0];
    const matchedCmd = this._findCommand(cmdName);

    if (matchedCmd) {
      args = args.slice(1);
      await matchedCmd._parseArgsAsync(args);
      return;
    }

    // Check for --help on root
    if (args.includes("--help") || args.includes("-h")) {
      this.help({ error: false });
    }

    this._maybePrintVersion(args);

    // Build option lookup
    const optionMap = this._buildOptionMap();
    const { optionValues, positionalArgs } = this._parseOptions(args, optionMap);

    // Apply defaults
    for (const opt of this.options) {
      const attr = opt.attributeName();
      if (!(attr in optionValues)) {
        if (opt.defaultValue !== undefined) {
          optionValues[attr] = opt.defaultValue;
        } else if (opt.negate) {
          optionValues[attr] = true;
        } else if (opt.isBoolean()) {
          optionValues[attr] = false;
        }
      }
    }

    // Handle negate options
    for (const opt of this.options) {
      if (opt.negate) {
        const attr = camelcase(opt.name().replace(/^--/, "").replace(/^no-/, ""));
        if (optionValues[attr] === true) {
          // --no-runtime was set → runtime = false
          // The negate flag name is --no-runtime, so the option key is "noRuntime"
          // but we want it as "runtime" = false
        }
      }
    }

    // Parse arguments
    const parsedArgs = this._parseArguments(positionalArgs);

    // Validate required arguments
    this._validateArguments(parsedArgs);

    // Validate mandatory options
    for (const opt of this.options) {
      if (opt.mandatory && opt.attributeName() in optionValues === false) {
        this._exit(1, "commander.missingMandatoryOptionValue", `option '${opt.flags}' is required`);
      }
    }

    // Store results
    this.args = positionalArgs;
    this.processedArgs = parsedArgs;
    this._opts = optionValues;

    // Run preAction hooks
    for (const hook of this._preActionHooks) {
      await hook(...parsedArgs, optionValues, this);
    }

    // Call action
    if (this._action) {
      await this._action(...parsedArgs, optionValues, this);
    }

    // Run postAction hooks
    for (const hook of this._postActionHooks) {
      await hook(...parsedArgs, optionValues, this);
    }
  }

  private _parseArgs(args: string[]): void {
    // Find matching command first
    const cmdName = args[0];
    const matchedCmd = this._findCommand(cmdName);

    if (matchedCmd) {
      args = args.slice(1);
      matchedCmd._parseArgs(args);
      return;
    }

    // Check for --help on root
    if (args.includes("--help") || args.includes("-h")) {
      this.help({ error: false });
    }

    this._maybePrintVersion(args);

    // Build option lookup
    const optionMap = this._buildOptionMap();
    const { optionValues, positionalArgs } = this._parseOptions(args, optionMap);

    // Apply defaults
    for (const opt of this.options) {
      const attr = opt.attributeName();
      if (!(attr in optionValues)) {
        if (opt.defaultValue !== undefined) {
          optionValues[attr] = opt.defaultValue;
        } else if (opt.negate) {
          optionValues[attr] = true;
        } else if (opt.isBoolean()) {
          optionValues[attr] = false;
        }
      }
    }

    // Parse arguments
    const parsedArgs = this._parseArguments(positionalArgs);

    // Validate required arguments
    this._validateArguments(parsedArgs);

    // Validate mandatory options
    for (const opt of this.options) {
      if (opt.mandatory && !(opt.attributeName() in optionValues)) {
        this._exit(1, "commander.missingMandatoryOptionValue", `option '${opt.flags}' is required`);
      }
    }

    // Store results
    this.args = positionalArgs;
    this.processedArgs = parsedArgs;
    this._opts = optionValues;

    // Run preAction hooks (sync only — mini-tsc does not support Promise yet)
    for (const hook of this._preActionHooks) {
      hook(...parsedArgs, optionValues, this);
    }

    // Call action (sync only)
    if (this._action) {
      this._action(...parsedArgs, optionValues, this);
    }

    // Run postAction hooks (sync only)
    for (const hook of this._postActionHooks) {
      hook(...parsedArgs, optionValues, this);
    }
  }

  private _findCommand(name: string): Command | null {
    if (!name) return null;

    // Check exact name match
    for (const cmd of this.commands) {
      if (cmd._name === name) return cmd;
    }

    // Check aliases
    for (const cmd of this.commands) {
      if (cmd._aliases.includes(name)) return cmd;
    }

    // Check default command
    for (const cmd of this.commands) {
      if (cmd._defaultCommand) return cmd;
    }

    return null;
  }

  private _buildOptionMap(): Map<string, Option> {
    const map = new Map<string, Option>();
    for (const opt of this.options) {
      if (opt.short) map.set(opt.short, opt);
      if (opt.long) map.set(opt.long, opt);
      // For --no-xxx, also map the positive form
      if (opt.negate && opt.long) {
        const positiveForm = opt.long.replace(/^--no-/, "--");
        map.set(positiveForm, opt);
      }
    }
    return map;
  }

  private _parseOptions(
    args: string[],
    optionMap: Map<string, Option>
  ): { optionValues: Record<string, unknown>; positionalArgs: string[] } {
    const optionValues: Record<string, unknown> = {};
    const positionalArgs: string[] = [];

    let i = 0;
    while (i < args.length) {
      const arg = args[i];

      if (arg === "--") {
        positionalArgs.push(...args.slice(i + 1));
        break;
      }

      if (arg.startsWith("-")) {
        const opt = optionMap.get(arg);
        if (opt) {
          // Handle --no-xxx negate options
          if (opt.negate) {
            const attr = opt.attributeName();
            optionValues[attr] = false;
            i++;
            continue;
          }

          if (opt.required || opt.optional) {
            // Next arg is the option value
            i++;
            if (i < args.length) {
              const value = args[i];
              if (opt.argChoices && !opt.argChoices.includes(value)) {
                this._exit(
                  1,
                  "commander.invalidArgument",
                  `option '${opt.flags}' argument '${value}' is not one of ${opt.argChoices.join(", ")}`
                );
              }
              if (opt.parseArg) {
                optionValues[opt.attributeName()] = opt.parseArg(value, optionValues[opt.attributeName()]);
              } else {
                optionValues[opt.attributeName()] = value;
              }
              i++;
            } else if (opt.optional) {
              // Use preset or default for optional args
              optionValues[opt.attributeName()] = opt.presetArg !== undefined
                ? opt.presetArg
                : opt.defaultValue;
              i++;
            } else {
              this._exit(1, "commander.optionMissingArgument", `option '${opt.flags}' requires an argument`);
            }
          } else if (opt.isBoolean()) {
            optionValues[opt.attributeName()] = true;
            i++;
          } else {
            i++;
          }
        } else if (arg.startsWith("--")) {
          if (!this._allowUnknownOption) {
            this._exit(1, "commander.unknownOption", `unknown option '${arg}'`);
          }
          positionalArgs.push(arg);
          i++;
        } else {
          // Short option: could be combined like -vd
          const shortOpts = arg.slice(1);
          let j = 0;
          while (j < shortOpts.length) {
            const ch = `-${shortOpts[j]}`;
            const opt = optionMap.get(ch);
            if (opt) {
              if (opt.required) {
                // Rest of combined flags is the value, or next arg
                if (j + 1 < shortOpts.length) {
                  const value = shortOpts.slice(j + 1);
                  optionValues[opt.attributeName()] = value;
                } else {
                  i++;
                  if (i < args.length) {
                    optionValues[opt.attributeName()] = args[i];
                  } else {
                    this._exit(1, "commander.optionMissingArgument", `option '${opt.flags}' requires an argument`);
                  }
                }
                break;
              } else if (opt.isBoolean()) {
                optionValues[opt.attributeName()] = true;
                j++;
              } else {
                j++;
              }
            } else {
              if (!this._allowUnknownOption) {
                this._exit(1, "commander.unknownOption", `unknown option '${ch}'`);
              }
              j++;
            }
          }
          i++;
        }
      } else {
        positionalArgs.push(arg);
        i++;
      }
    }

    return { optionValues, positionalArgs };
  }

  private _parseArguments(positionalArgs: string[]): string[] {
    const parsed: string[] = [];
    let argIdx = 0;

    for (const argDef of this.arguments) {
      if (argDef.variadic) {
        const remaining = positionalArgs.slice(argIdx);
        if (argDef.required && remaining.length === 0) {
          this._exit(1, "commander.missingArgument", `required argument '${argDef.name()}' not provided`);
        }
        parsed.push(...remaining);
        argIdx = positionalArgs.length;
      } else {
        if (argIdx < positionalArgs.length) {
          const value = positionalArgs[argIdx];
          if (argDef.argChoices && !argDef.argChoices.includes(value)) {
            this._exit(
              1,
              "commander.invalidArgument",
              `argument '${value}' is not one of ${argDef.argChoices.join(", ")}`
            );
          }
          parsed.push(value);
          argIdx++;
        } else if (argDef.required) {
          this._exit(1, "commander.missingArgument", `required argument '${argDef.name()}' not provided`);
        } else if (argDef.defaultValue !== undefined) {
          parsed.push(String(argDef.defaultValue));
        }
      }
    }

    return parsed;
  }

  private _validateArguments(parsedArgs: string[]): void {
    if (!this._allowExcessArguments) {
      if (parsedArgs.length > this.arguments.length) {
        const extra = parsedArgs.length - this.arguments.length;
        this._exit(1, "commander.excessArguments", `too many arguments (${parsedArgs.length} for ${this.arguments.length})`);
      }
    }
  }

  private _exit(exitCode: number, code: string, message: string): never {
    if (this._exitOverride) {
      throw new CommanderError(exitCode, code, message);
    }
    if (exitCode !== 0) {
      process.stderr.write(message + "\n");
      process.exit(exitCode);
    }
    process.exit(0);
  }
}
