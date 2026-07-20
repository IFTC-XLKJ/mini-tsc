/*
 * Stub C implementation for src/cli/commander/index.
 * All functions return dummy values — this file exists solely so that
 * mini-tsc-compiled programs that import the commander module can link.
 */
#include "src_cli_commander_index.h"

/* ---- Module-level exports ---- */
Value src_cli_commander_index_Command(void)       { return ts_value_undefined(); }
Value src_cli_commander_index_createCommand(void) { return ts_value_undefined(); }
Value src_cli_commander_index_program(void)       { return ts_value_undefined(); }
Value src_cli_commander_index_Option(void)        { return ts_value_undefined(); }
Value src_cli_commander_index_Argument(void)      { return ts_value_undefined(); }

/* ---- Command class stubs ---- */
static Command _cmd_stub;

Command* Command_constructor(void) { return &_cmd_stub; }
Command* Command_constructor_1(Value name) { (void)name; return &_cmd_stub; }

Value Command_name(Command* cmd, Value name)             { (void)cmd; (void)name; return ts_value_undefined(); }
Value Command_description(Command* cmd, Value desc)      { (void)cmd; (void)desc; return ts_value_undefined(); }
Value Command_version(Command* cmd, Value ver, ...)      { (void)cmd; (void)ver; return ts_value_undefined(); }
Value Command_option(Command* cmd, Value flags, Value desc, ...) { (void)cmd; (void)flags; (void)desc; return ts_value_undefined(); }
Value Command_requiredOption(Command* cmd, Value flags, Value desc, ...) { (void)cmd; (void)flags; (void)desc; return ts_value_undefined(); }
Value Command_argument(Command* cmd, Value name, Value desc, ...) { (void)cmd; (void)name; (void)desc; return ts_value_undefined(); }
Value Command_action(Command* cmd, Value fn)             { (void)cmd; (void)fn; return ts_value_undefined(); }
Value Command_parse(Command* cmd, Value argv, Value options) { (void)cmd; (void)argv; (void)options; return ts_value_undefined(); }
Value Command_parseAsync(Command* cmd, Value argv, Value options) { (void)cmd; (void)argv; (void)options; return ts_value_undefined(); }
Value Command_opts(Command* cmd)                         { (void)cmd; return ts_value_object(ts_hashmap_new()); }
Command* Command_command(Command* cmd, Value nameAndArgs, ...) { (void)cmd; (void)nameAndArgs; return &_cmd_stub; }
Value Command_alias(Command* cmd, Value alias)           { (void)cmd; (void)alias; return ts_value_undefined(); }
Value Command_addOption(Command* cmd, Option* opt)       { (void)cmd; (void)opt; return ts_value_undefined(); }
Value Command_addArgument(Command* cmd, Value arg)       { (void)cmd; (void)arg; return ts_value_undefined(); }
Value Command_addCommand(Command* cmd, Value sub)        { (void)cmd; (void)sub; return ts_value_undefined(); }
Value Command_help(Command* cmd, Value context)          { (void)cmd; (void)context; return ts_value_undefined(); }
Value Command_outputHelp(Command* cmd)                   { (void)cmd; return ts_value_undefined(); }
Value Command_configureOutput(Command* cmd, Value config) { (void)cmd; (void)config; return ts_value_undefined(); }
Value Command_exitOverride(Command* cmd)                 { (void)cmd; return ts_value_undefined(); }
Value Command_allowUnknownOption(Command* cmd, Value allow) { (void)cmd; (void)allow; return ts_value_undefined(); }
Value Command_allowExcessArguments(Command* cmd, Value allow) { (void)cmd; (void)allow; return ts_value_undefined(); }
Value Command_getName(Command* cmd)                      { (void)cmd; return ts_value_string(ts_string_new("")); }
Value Command_getDescription(Command* cmd)               { (void)cmd; return ts_value_string(ts_string_new("")); }
Value Command_getVersion(Command* cmd)                   { (void)cmd; return ts_value_undefined(); }
Value Command_getAlias(Command* cmd)                     { (void)cmd; return ts_value_undefined(); }
Value Command_hook(Command* cmd, Value event, Value fn)  { (void)cmd; (void)event; (void)fn; return ts_value_undefined(); }
Value Command_hidden(Command* cmd)                       { (void)cmd; return ts_value_undefined(); }

/* ---- Option class stubs ---- */
static Option _opt_stub;

Option* Option_constructor(Value flags, Value desc) { (void)flags; (void)desc; return &_opt_stub; }
Value Option_default(Option* opt, Value val, ...)        { (void)opt; (void)val; return ts_value_undefined(); }
Value Option_choices(Option* opt, Value values)          { (void)opt; (void)values; return ts_value_undefined(); }
Value Option_argParser(Option* opt, Value fn)            { (void)opt; (void)fn; return ts_value_undefined(); }
Value Option_makeOptionMandatory(Option* opt, Value mandatory) { (void)opt; (void)mandatory; return ts_value_undefined(); }
Value Option_hideHelp(Option* opt, Value hide)           { (void)opt; (void)hide; return ts_value_undefined(); }
Value Option_attributeName(Option* opt)                  { (void)opt; return ts_value_string(ts_string_new("")); }
Value Option_name(Option* opt)                           { (void)opt; return ts_value_string(ts_string_new("")); }
Value Option_isBoolean(Option* opt)                      { (void)opt; return ts_value_boolean(0); }
Value Option_preset(Option* opt, Value arg)              { (void)opt; (void)arg; return ts_value_undefined(); }
Value Option_conflicts(Option* opt, Value names)         { (void)opt; (void)names; return ts_value_undefined(); }
Value Option_implies(Option* opt, Value values)          { (void)opt; (void)values; return ts_value_undefined(); }
Value Option_env(Option* opt, Value name)                { (void)opt; (void)name; return ts_value_undefined(); }
