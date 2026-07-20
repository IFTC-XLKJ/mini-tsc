/*
 * Stub header + declarations for src/cli/commander/index.
 * Provides opaque types and function prototypes matching the transpiler output.
 */
#ifndef SRC_CLI_COMMANDER_INDEX_H
#define SRC_CLI_COMMANDER_INDEX_H

#include "runtime.h"
#include <stdarg.h>

/* Opaque class types */
typedef struct Command_s { int _dummy; } Command;
typedef struct Option_s  { int _dummy; } Option;

/* Module-level exports (as transpiler generates) */
Value src_cli_commander_index_Command(void);
Value src_cli_commander_index_createCommand(void);
Value src_cli_commander_index_program(void);
Value src_cli_commander_index_Option(void);
Value src_cli_commander_index_Argument(void);

/* Command class — all methods take first arg as Command* */
Command* Command_constructor(void);
Command* Command_constructor_1(Value name);
Value Command_name(Command* cmd, Value name);
Value Command_description(Command* cmd, Value desc);
Value Command_version(Command* cmd, Value ver, ...);
Value Command_option(Command* cmd, Value flags, Value desc, ...);
Value Command_requiredOption(Command* cmd, Value flags, Value desc, ...);
Value Command_argument(Command* cmd, Value name, Value desc, ...);
Value Command_action(Command* cmd, Value fn);
Value Command_parse(Command* cmd, Value argv, Value options);
Value Command_parseAsync(Command* cmd, Value argv, Value options);
Value Command_opts(Command* cmd);
Command* Command_command(Command* cmd, Value nameAndArgs, ...);
Value Command_alias(Command* cmd, Value alias);
Value Command_addOption(Command* cmd, Option* opt);
Value Command_addArgument(Command* cmd, Value arg);
Value Command_addCommand(Command* cmd, Value sub);
Value Command_help(Command* cmd, Value context);
Value Command_outputHelp(Command* cmd);
Value Command_configureOutput(Command* cmd, Value config);
Value Command_exitOverride(Command* cmd);
Value Command_allowUnknownOption(Command* cmd, Value allow);
Value Command_allowExcessArguments(Command* cmd, Value allow);
Value Command_getName(Command* cmd);
Value Command_getDescription(Command* cmd);
Value Command_getVersion(Command* cmd);
Value Command_getAlias(Command* cmd);
Value Command_hook(Command* cmd, Value event, Value fn);
Value Command_hidden(Command* cmd);

/* Option class */
Option* Option_constructor(Value flags, Value desc);
Value Option_default(Option* opt, Value val, ...);
Value Option_choices(Option* opt, Value values);
Value Option_argParser(Option* opt, Value fn);
Value Option_makeOptionMandatory(Option* opt, Value mandatory);
Value Option_hideHelp(Option* opt, Value hide);
Value Option_attributeName(Option* opt);
Value Option_name(Option* opt);
Value Option_isBoolean(Option* opt);
Value Option_preset(Option* opt, Value arg);
Value Option_conflicts(Option* opt, Value names);
Value Option_implies(Option* opt, Value values);
Value Option_env(Option* opt, Value name);

#endif /* SRC_CLI_COMMANDER_INDEX_H */
