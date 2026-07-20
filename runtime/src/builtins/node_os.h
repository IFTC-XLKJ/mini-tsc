#ifndef NODE_OS_H
#define NODE_OS_H

#include "runtime.h"

Value node_os_platform(void);
Value node_os_hostname(void);
double node_os_totalmem(void);
double node_os_freemem(void);
Value node_os_arch(void);
Value node_os_cpus(void);
Value node_os_userInfo(void);
Value node_os_type(void);
Value node_os_release(void);
double node_os_uptime(void);
Value node_os_loadavg(void);
Value node_os_homedir(void);
Value node_os_tmpdir(void);
Value node_os_version(void);
Value node_os_machine(void);
Value node_os_EOL(void);
Value node_os_devNull(void);

#endif /* NODE_OS_H */
