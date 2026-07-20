#ifndef NODE_FS_H
#define NODE_FS_H

#include "runtime.h"

/* Synchronous functions */
Value node_fs_readFileSync(Value path, Value options);
int node_fs_writeFileSync(Value path, Value data, Value options);
int node_fs_existsSync(Value path);
int node_fs_mkdirSync(Value path, Value options);
Value node_fs_readdirSync(Value path);
int node_fs_unlinkSync(Value path);
Value node_fs_statSync(Value path);
int node_fs_rmdirSync(Value path);
int node_fs_renameSync(Value oldPath, Value newPath);
Value node_fs_readlinkSync(Value path);
int node_fs_symlinkSync(Value target, Value path);
int node_fs_chmodSync(Value path, Value mode);

/* Asynchronous functions (return Value which can be awaited) */
Value node_fs_readFile(Value path, Value options);
Value node_fs_writeFile(Value path, Value data, Value options);
Value node_fs_access(Value path, Value mode);
Value node_fs_mkdir(Value path, Value options);
Value node_fs_readdir(Value path);
Value node_fs_unlink(Value path);
Value node_fs_stat(Value path);
Value node_fs_rmdir(Value path);
Value node_fs_rename(Value oldPath, Value newPath);
Value node_fs_readlink(Value path);
Value node_fs_symlink(Value target, Value path);
Value node_fs_chmod(Value path, Value mode);

#endif /* NODE_FS_H */
