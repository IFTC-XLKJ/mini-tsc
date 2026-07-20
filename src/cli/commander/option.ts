export class Option {
  flags: string;
  description: string;
  required: boolean;
  optional: boolean;
  variadic: boolean;
  mandatory: boolean;
  short?: string;
  long?: string;
  negate: boolean;
  defaultValue?: unknown;
  defaultValueDescription?: string;
  presetArg?: unknown;
  parseArg?: <T>(value: string, previous: T) => T;
  hidden: boolean;
  argChoices?: string[];
  envVar?: string;

  private _conflicts: string[] = [];
  private _implies?: Record<string, unknown>;

  constructor(flags: string, description: string = "") {
    this.flags = flags;
    this.description = description;
    this.hidden = false;
    this.mandatory = false;
    this.variadic = false;
    this.negate = false;
    this.required = false;
    this.optional = false;
    // Always init string fields so help never reads NULL pointers
    this.short = "";
    this.long = "";

    // Parse flags string: "-o, --output <name>" or "--no-runtime"
    // Manual scan only — regex split is not supported in the C backend.
    let shortFlag = "";
    let longFlag = "";
    let required = false;
    let optional = false;
    let variadic = false;
    let negate = false;

    let i = 0;
    while (i < flags.length) {
      const c = flags.charAt(i);
      if (c === " ") {
        i++;
        continue;
      }
      if (c === "\t") {
        i++;
        continue;
      }
      if (c === ",") {
        i++;
        continue;
      }
      if (c === "|") {
        i++;
        continue;
      }

      // Value token: <name> or [name] or <name...>
      if (c === "<") {
        let token = "";
        i++;
        while (i < flags.length) {
          const ch = flags.charAt(i);
          if (ch === ">") {
            i++;
            break;
          }
          token = token + ch;
          i++;
        }
        required = true;
        if (token.indexOf("...") >= 0) {
          variadic = true;
        }
        continue;
      }
      if (c === "[") {
        let token = "";
        i++;
        while (i < flags.length) {
          const ch = flags.charAt(i);
          if (ch === "]") {
            i++;
            break;
          }
          token = token + ch;
          i++;
        }
        optional = true;
        if (token.indexOf("...") >= 0) {
          variadic = true;
        }
        continue;
      }

      // Flag token: starts with -
      if (c === "-") {
        let token = "";
        while (i < flags.length) {
          const ch = flags.charAt(i);
          if (ch === " ") break;
          if (ch === "\t") break;
          if (ch === ",") break;
          if (ch === "|") break;
          if (ch === "<") break;
          if (ch === "[") break;
          token = token + ch;
          i++;
        }
        if (token.indexOf("--no-") === 0) {
          longFlag = token;
          negate = true;
        } else if (token.indexOf("--") === 0) {
          longFlag = token;
        } else {
          shortFlag = token;
        }
        continue;
      }

      // Skip unknown char
      i++;
    }

    this.short = shortFlag;
    this.long = longFlag;
    this.required = required;
    this.optional = optional;
    this.variadic = variadic;
    this.negate = negate;
  }

  default(value: unknown, description?: string): this {
    this.defaultValue = value;
    this.defaultValueDescription = description;
    return this;
  }

  preset(arg: unknown): this {
    this.presetArg = arg;
    return this;
  }

  conflicts(names: string | string[]): this {
    if (typeof names === "string") {
      this._conflicts.push(names);
    } else {
      for (const n of names) {
        this._conflicts.push(n);
      }
    }
    return this;
  }

  implies(optionValues: Record<string, unknown>): this {
    this._implies = optionValues;
    return this;
  }

  env(name: string): this {
    this.envVar = name;
    return this;
  }

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  argParser(fn: (value: string, previous: any) => any): this {
    this.parseArg = fn;
    return this;
  }

  makeOptionMandatory(mandatory: boolean = true): this {
    this.mandatory = mandatory;
    return this;
  }

  hideHelp(hide: boolean = true): this {
    this.hidden = hide;
    return this;
  }

  choices(values: readonly string[]): this {
    // Assign wholesale — element push of string[] is mishandled by the C backend.
    this.argChoices = values as string[];
    return this;
  }

  name(): string {
    if (this.long && this.long.length > 0) return this.long;
    if (this.short && this.short.length > 0) return this.short;
    return "";
  }

  attributeName(): string {
    let n = this.name();
    // Strip leading -- or -
    if (n.indexOf("--") === 0) {
      n = n.substring(2);
    } else if (n.indexOf("-") === 0) {
      n = n.substring(1);
    }
    // Strip leading no-
    if (n.indexOf("no-") === 0) {
      n = n.substring(3);
    }
    return camelcase(n);
  }

  isBoolean(): boolean {
    if (this.required) return false;
    if (this.optional) return false;
    if (this.variadic) return false;
    return true;
  }

  static from(flags: string, description?: string): Option {
    return new Option(flags, description);
  }
}

/** camelCase from kebab-case without reduce/regex (C backend safe). */
function camelcase(str: string): string {
  if (str.length === 0) return "";
  let result = "";
  let upperNext = false;
  let first = true;
  for (let i = 0; i < str.length; i++) {
    const ch = str.charAt(i);
    if (ch === "-") {
      upperNext = true;
      continue;
    }
    if (upperNext) {
      if (!first) {
        result = result + ch.toUpperCase();
      } else {
        result = result + ch;
      }
      upperNext = false;
    } else {
      result = result + ch;
    }
    first = false;
  }
  return result;
}

export { camelcase };
