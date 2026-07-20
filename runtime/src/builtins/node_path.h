#ifndef NODE_PATH_H
#define NODE_PATH_H

#include "runtime.h"

Value node_path_join(Value* args, int argc);
Value node_path_resolve(Value* args, int argc);
Value node_path_basename(Value path, Value ext);
Value node_path_dirname(Value path);
Value node_path_extname(Value path);
Value node_path_normalize(Value path);
Value node_path_parse(Value path);
Value node_path_format(Value pathObject);
Value node_path_isAbsolute(Value path);
Value node_path_relative(Value from, Value to);

#endif /* NODE_PATH_H */
