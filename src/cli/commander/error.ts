export class CommanderError extends Error {
  code: string;
  exitCode: number;
  message: string;
  nestedError?: string;

  constructor(exitCode: number, code: string, message: string) {
    super(message);
    this.name = "CommanderError";
    this.code = code;
    this.exitCode = exitCode;
    this.message = message;
  }
}

export class InvalidArgumentError extends CommanderError {
  constructor(message: string) {
    super(1, "commander.invalidArgument", message);
    this.name = "InvalidArgumentError";
  }
}
