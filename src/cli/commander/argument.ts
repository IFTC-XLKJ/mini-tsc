export class Argument {
  description: string;
  required: boolean;
  variadic: boolean;
  defaultValue?: unknown;
  defaultValueDescription?: string;
  argChoices?: string[];
  private _argParser?: (value: string, previous: unknown) => unknown;
  private _name: string;

  constructor(arg: string, description: string = "") {
    this.description = description;
    this.variadic = false;

    const name = arg.replace(/[<\>[]/g, "").replace(/\.\.\./, "");
    this._name = name;

    // Determine required vs optional
    this.required = arg.startsWith("<");
    this.variadic = arg.includes("...");
  }

  name(): string {
    return this._name;
  }

  default(value: unknown, description?: string): this {
    this.defaultValue = value;
    this.defaultValueDescription = description;
    return this;
  }

  argParser<T>(fn: (value: string, previous: T) => T): this {
    this._argParser = fn as (value: string, previous: unknown) => unknown;
    return this;
  }

  choices(values: readonly string[]): this {
    this.argChoices = [...values];
    return this;
  }

  argRequired(): this {
    this.required = true;
    return this;
  }

  argOptional(): this {
    this.required = false;
    return this;
  }
}
