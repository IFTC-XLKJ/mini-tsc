import { Command } from "./command.js";
import { Option, camelcase } from "./option.js";
import { Argument } from "./argument.js";
import { CommanderError, InvalidArgumentError } from "./error.js";

// Default program instance
const program = new Command();

export {
  Command,
  Option,
  Argument,
  CommanderError,
  InvalidArgumentError,
  camelcase,
  program,
};

// createCommand factory
export function createCommand(name?: string): Command {
  return new Command(name);
}
