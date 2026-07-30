#include "node_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CPUS 256

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifdef _WIN32
/* Helper: run a WMI query via PowerShell and extract a single value */
static Value wmi_query(const char* wmiClass, const char* property) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
    "powershell -NoProfile -Command \"(Get-CimInstance -ClassName %s).%s\" 2>NUL",
    wmiClass, property);
  FILE* pipe = _popen(cmd, "r");
  if (!pipe) return ts_value_string(ts_string_new(""));
  char buf[512] = "";
  if (fgets(buf, sizeof(buf), pipe)) {
    /* Trim trailing newline */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
  }
  _pclose(pipe);
  return ts_value_string(ts_string_new(buf));
}
#else
/* Helper: read a DMI sysfs file */
static Value dmi_read(const char* field) {
  char path[256];
  snprintf(path, sizeof(path), "/sys/class/dmi/id/%s", field);
  FILE* f = fopen(path, "r");
  if (!f) return ts_value_string(ts_string_new(""));
  char buf[512] = "";
  if (fgets(buf, sizeof(buf), f)) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
  }
  fclose(f);
  return ts_value_string(ts_string_new(buf));
}
#endif

Value node_os_platform(void) {
#ifdef _WIN32
  return ts_value_string(ts_string_new("win32"));
#elif __APPLE__
  return ts_value_string(ts_string_new("darwin"));
#elif __linux__
  return ts_value_string(ts_string_new("linux"));
#else
  return ts_value_string(ts_string_new("unknown"));
#endif
}

Value node_os_hostname(void) {
#ifdef _WIN32
  char buf[256];
  DWORD len = 256;
  GetComputerNameA(buf, &len);
  return ts_value_string(ts_string_new(buf));
#else
  char buf[256];
  gethostname(buf, sizeof(buf));
  return ts_value_string(ts_string_new(buf));
#endif
}

double node_os_totalmem(void) {
#ifdef _WIN32
  MEMORYSTATUSEX stat;
  stat.dwLength = sizeof(stat);
  GlobalMemoryStatusEx(&stat);
  return (double)stat.ullTotalPhys;
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long pageSize = sysconf(_SC_PAGE_SIZE);
  return (double)pages * (double)pageSize;
#endif
}

double node_os_freemem(void) {
#ifdef _WIN32
  MEMORYSTATUSEX stat;
  stat.dwLength = sizeof(stat);
  GlobalMemoryStatusEx(&stat);
  return (double)stat.ullAvailPhys;
#else
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long pageSize = sysconf(_SC_PAGE_SIZE);
  return (double)pages * (double)pageSize;
#endif
}

Value node_os_arch(void) {
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return ts_value_string(ts_string_new("x64"));
    case PROCESSOR_ARCHITECTURE_ARM64: return ts_value_string(ts_string_new("arm64"));
    default: return ts_value_string(ts_string_new("x86"));
  }
#elif __x86_64__ || __x86_64
  return ts_value_string(ts_string_new("x64"));
#elif __aarch64__
  return ts_value_string(ts_string_new("arm64"));
#else
  return ts_value_string(ts_string_new("x86"));
#endif
}

Value node_os_cpus(void) {
  TSArray* arr = ts_array_new();
  char model[256] = "unknown";
  double speed = 0;
  int ncpu = 1;

#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  ncpu = (int)info.dwNumberOfProcessors;
  if (ncpu < 1) ncpu = 1;

  /* CPU brand string via CPUID leaf 0x80000002-4 (MSVC/clang intrinsic) */
  int cpuInfo[4] = {0};
  char brand[49] = {0};
  __cpuid(cpuInfo, 0x80000000);
  unsigned int maxExt = (unsigned int)cpuInfo[0];
  if (maxExt >= 0x80000004) {
    __cpuid((int*)(brand + 0), 0x80000002);
    __cpuid((int*)(brand + 16), 0x80000003);
    __cpuid((int*)(brand + 32), 0x80000004);
    char* p = brand;
    while (*p == ' ') p++;
    size_t i = 0;
    while (p[i] && i < sizeof(model) - 1) {
      model[i] = p[i];
      i++;
    }
    model[i] = '\0';
  }

  /* Per-core CPU times via sampling */
  {
    static ULARGE_INTEGER prev_idle = {0}, prev_kernel = {0}, prev_user = {0};
    static int first_call = 1;

    FILETIME idleTime, kernelTime, userTime;
    GetSystemTimes(&idleTime, &kernelTime, &userTime);

    ULARGE_INTEGER cur_idle, cur_kernel, cur_user;
    cur_idle.LowPart = idleTime.dwLowDateTime;
    cur_idle.HighPart = idleTime.dwHighDateTime;
    cur_kernel.LowPart = kernelTime.dwLowDateTime;
    cur_kernel.HighPart = kernelTime.dwHighDateTime;
    cur_user.LowPart = userTime.dwLowDateTime;
    cur_user.HighPart = userTime.dwHighDateTime;

    double usage = 0;
    if (!first_call) {
      ULONGLONG d_idle = cur_idle.QuadPart - prev_idle.QuadPart;
      ULONGLONG d_kernel = cur_kernel.QuadPart - prev_kernel.QuadPart;
      ULONGLONG d_user = cur_user.QuadPart - prev_user.QuadPart;
      ULONGLONG d_total = d_kernel + d_user;
      if (d_total > 0) {
        usage = (double)(d_total - d_idle) / (double)d_total * 100.0;
      }
    }
    first_call = 0;
    prev_idle = cur_idle;
    prev_kernel = cur_kernel;
    prev_user = cur_user;

    for (int i = 0; i < ncpu; i++) {
      TSHashMap* cpu = ts_hashmap_new();
      ts_hashmap_set(cpu, ts_string_new("model"), ts_value_string(ts_string_new(model)));
      ts_hashmap_set(cpu, ts_string_new("speed"), ts_value_number(speed));
      TSHashMap* times = ts_hashmap_new();
      ts_hashmap_set(times, ts_string_new("user"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("nice"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("sys"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("idle"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("irq"), ts_value_number(0));
      ts_hashmap_set(cpu, ts_string_new("times"), ts_value_object(times));
      ts_hashmap_set(cpu, ts_string_new("usage"), ts_value_number(usage));
      ts_array_push(arr, ts_value_object(cpu));
    }
  }
#else
  ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (ncpu < 1) ncpu = 1;
  FILE* f = fopen("/proc/cpuinfo", "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "model name", 10) == 0) {
        char* colon = strchr(line, ':');
        if (colon) {
          colon++;
          while (*colon == ' ' || *colon == '\t') colon++;
          size_t len = strlen(colon);
          while (len > 0 && (colon[len - 1] == '\n' || colon[len - 1] == '\r')) {
            colon[--len] = '\0';
          }
          size_t i = 0;
          while (colon[i] && i < sizeof(model) - 1) {
            model[i] = colon[i];
            i++;
          }
          model[i] = '\0';
        }
        break;
      }
      if (strncmp(line, "cpu MHz", 7) == 0) {
        char* colon = strchr(line, ':');
        if (colon) speed = atof(colon + 1);
      }
    }
    fclose(f);
  }

  /* Per-core CPU times from /proc/stat */
  {
    static unsigned long long prev_idle[MAX_CPUS] = {0};
    static unsigned long long prev_total[MAX_CPUS] = {0};
    static int first_call = 1;
    double usages[MAX_CPUS] = {0};

    FILE* sf = fopen("/proc/stat", "r");
    if (sf) {
      char line[1024];
      int core = -1; /* -1 = aggregate "cpu " line */
      while (fgets(line, sizeof(line), sf)) {
        if (strncmp(line, "cpu", 3) != 0) break;
        core++;
        if (core >= ncpu) break;

        /* Skip aggregate "cpu " line (core=0 maps to first per-cpu line) */
        if (line[3] == ' ') { core = 0; } /* "cpu " aggregate → will be overwritten by "cpu0" */
        else {
          /* "cpu0", "cpu1", etc. — extract core index */
          int idx = atoi(line + 3);
          if (idx >= ncpu) continue;
          core = idx;
        }

        unsigned long long user, nice, sys, idle, irq, softirq, steal;
        char* p = strchr(line, ' ');
        if (!p) continue;
        user = strtoull(p, &p, 10);
        nice = strtoull(p, &p, 10);
        sys = strtoull(p, &p, 10);
        idle = strtoull(p, &p, 10);
        irq = strtoull(p, &p, 10);
        softirq = strtoull(p, &p, 10);
        steal = strtoull(p, &p, 10);

        unsigned long long total = user + nice + sys + idle + irq + softirq + steal;

        if (!first_call && core < MAX_CPUS) {
          unsigned long long d_idle = idle - prev_idle[core];
          unsigned long long d_total = total - prev_total[core];
          if (d_total > 0) {
            usages[core] = (double)(d_total - d_idle) / (double)d_total * 100.0;
          }
        }
        if (core < MAX_CPUS) {
          prev_idle[core] = idle;
          prev_total[core] = total;
        }
      }
      fclose(sf);
      first_call = 0;
    }

    for (int i = 0; i < ncpu; i++) {
      TSHashMap* cpu = ts_hashmap_new();
      ts_hashmap_set(cpu, ts_string_new("model"), ts_value_string(ts_string_new(model)));
      ts_hashmap_set(cpu, ts_string_new("speed"), ts_value_number(speed));
      TSHashMap* times = ts_hashmap_new();
      ts_hashmap_set(times, ts_string_new("user"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("nice"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("sys"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("idle"), ts_value_number(0));
      ts_hashmap_set(times, ts_string_new("irq"), ts_value_number(0));
      ts_hashmap_set(cpu, ts_string_new("times"), ts_value_object(times));
      ts_hashmap_set(cpu, ts_string_new("usage"), ts_value_number(i < MAX_CPUS ? usages[i] : 0));
      ts_array_push(arr, ts_value_object(cpu));
    }
  }
#endif

  return ts_value_array(arr);
}

Value node_os_userInfo(void) {
  TSHashMap* info = ts_hashmap_new();
#ifdef _WIN32
  char username[256] = "unknown";
  DWORD len = 256;
  GetUserNameA(username, &len);
  char homedir[MAX_PATH] = "";
  const char* userprofile = getenv("USERPROFILE");
  if (userprofile) {
    size_t i = 0;
    while (userprofile[i] && i < sizeof(homedir) - 1) {
      homedir[i] = userprofile[i];
      i++;
    }
    homedir[i] = '\0';
  }
  ts_hashmap_set(info, ts_string_new("uid"), ts_value_number(-1));
  ts_hashmap_set(info, ts_string_new("gid"), ts_value_number(-1));
  ts_hashmap_set(info, ts_string_new("username"), ts_value_string(ts_string_new(username)));
  ts_hashmap_set(info, ts_string_new("homedir"), ts_value_string(ts_string_new(homedir[0] ? homedir : "")));
  ts_hashmap_set(info, ts_string_new("shell"), ts_value_null());
#else
  const char* user = getenv("USER");
  if (!user) user = getenv("LOGNAME");
  if (!user) user = "unknown";
  const char* home = getenv("HOME");
  if (!home) home = "";
  const char* shell = getenv("SHELL");
  if (!shell) shell = "";
  ts_hashmap_set(info, ts_string_new("uid"), ts_value_number((double)getuid()));
  ts_hashmap_set(info, ts_string_new("gid"), ts_value_number((double)getgid()));
  ts_hashmap_set(info, ts_string_new("username"), ts_value_string(ts_string_new(user)));
  ts_hashmap_set(info, ts_string_new("homedir"), ts_value_string(ts_string_new(home)));
  ts_hashmap_set(info, ts_string_new("shell"), ts_value_string(ts_string_new(shell)));
#endif
  return ts_value_object(info);
}

Value node_os_type(void) {
#ifdef _WIN32
  return ts_value_string(ts_string_new("Windows_NT"));
#elif __APPLE__
  return ts_value_string(ts_string_new("Darwin"));
#elif __linux__
  return ts_value_string(ts_string_new("Linux"));
#else
  return ts_value_string(ts_string_new("Unknown"));
#endif
}

Value node_os_release(void) {
#ifdef _WIN32
  OSVERSIONINFOA vi;
  memset(&vi, 0, sizeof(vi));
  vi.dwOSVersionInfoSize = sizeof(vi);
  /* GetVersionEx is deprecated but fine for a simple release string */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  GetVersionExA(&vi);
#pragma clang diagnostic pop
  char buf[64];
  snprintf(buf, sizeof(buf), "%lu.%lu.%lu",
           (unsigned long)vi.dwMajorVersion,
           (unsigned long)vi.dwMinorVersion,
           (unsigned long)vi.dwBuildNumber);
  return ts_value_string(ts_string_new(buf));
#else
  struct utsname u;
  if (uname(&u) == 0) {
    return ts_value_string(ts_string_new(u.release));
  }
  return ts_value_string(ts_string_new("unknown"));
#endif
}

double node_os_uptime(void) {
#ifdef _WIN32
  return (double)GetTickCount64() / 1000.0;
#else
  /* /proc/uptime first field */
  FILE* f = fopen("/proc/uptime", "r");
  if (f) {
    double up = 0;
    if (fscanf(f, "%lf", &up) == 1) {
      fclose(f);
      return up;
    }
    fclose(f);
  }
  return 0;
#endif
}

Value node_os_loadavg(void) {
  TSArray* arr = ts_array_new();
#ifdef _WIN32
  /* Windows has no loadavg — Node returns [0,0,0] */
  ts_array_push(arr, ts_value_number(0));
  ts_array_push(arr, ts_value_number(0));
  ts_array_push(arr, ts_value_number(0));
#else
  double avg[3] = {0, 0, 0};
  if (getloadavg(avg, 3) == 3) {
    ts_array_push(arr, ts_value_number(avg[0]));
    ts_array_push(arr, ts_value_number(avg[1]));
    ts_array_push(arr, ts_value_number(avg[2]));
  } else {
    ts_array_push(arr, ts_value_number(0));
    ts_array_push(arr, ts_value_number(0));
    ts_array_push(arr, ts_value_number(0));
  }
#endif
  return ts_value_array(arr);
}

Value node_os_homedir(void) {
#ifdef _WIN32
  const char* home = getenv("USERPROFILE");
  if (!home) home = getenv("HOME");
  if (!home) home = "";
  return ts_value_string(ts_string_new(home));
#else
  const char* home = getenv("HOME");
  if (!home) home = "";
  return ts_value_string(ts_string_new(home));
#endif
}

Value node_os_tmpdir(void) {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetTempPathA(MAX_PATH, buf);
  if (n > 0 && n < MAX_PATH) {
    /* Strip trailing backslash for consistency with Node when not root */
    if (n > 1 && (buf[n - 1] == '\\' || buf[n - 1] == '/')) {
      buf[n - 1] = '\0';
    }
    return ts_value_string(ts_string_new(buf));
  }
  const char* t = getenv("TEMP");
  if (!t) t = getenv("TMP");
  if (!t) t = "C:\\Windows\\Temp";
  return ts_value_string(ts_string_new(t));
#else
  const char* t = getenv("TMPDIR");
  if (!t) t = getenv("TMP");
  if (!t) t = getenv("TEMP");
  if (!t) t = "/tmp";
  return ts_value_string(ts_string_new(t));
#endif
}

Value node_os_version(void) {
#ifdef _WIN32
  /* Prefer kernel version string from RtlGetVersion-like data via GetVersionEx */
  OSVERSIONINFOEXA vi;
  memset(&vi, 0, sizeof(vi));
  vi.dwOSVersionInfoSize = sizeof(vi);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  GetVersionExA((OSVERSIONINFOA*)&vi);
#pragma clang diagnostic pop
  char buf[128];
  snprintf(buf, sizeof(buf), "Windows %lu.%lu Build %lu",
           (unsigned long)vi.dwMajorVersion,
           (unsigned long)vi.dwMinorVersion,
           (unsigned long)vi.dwBuildNumber);
  return ts_value_string(ts_string_new(buf));
#else
  struct utsname u;
  if (uname(&u) == 0) {
    return ts_value_string(ts_string_new(u.version));
  }
  return ts_value_string(ts_string_new("unknown"));
#endif
}

Value node_os_machine(void) {
#ifdef _WIN32
  SYSTEM_INFO info;
  GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return ts_value_string(ts_string_new("x86_64"));
    case PROCESSOR_ARCHITECTURE_ARM64: return ts_value_string(ts_string_new("arm64"));
    case PROCESSOR_ARCHITECTURE_ARM: return ts_value_string(ts_string_new("arm"));
    case PROCESSOR_ARCHITECTURE_IA64: return ts_value_string(ts_string_new("ia64"));
    default: return ts_value_string(ts_string_new("i386"));
  }
#else
  struct utsname u;
  if (uname(&u) == 0) {
    return ts_value_string(ts_string_new(u.machine));
  }
  return ts_value_string(ts_string_new("unknown"));
#endif
}

Value node_os_EOL(void) {
#ifdef _WIN32
  return ts_value_string(ts_string_new("\r\n"));
#else
  return ts_value_string(ts_string_new("\n"));
#endif
}

Value node_os_devNull(void) {
#ifdef _WIN32
  return ts_value_string(ts_string_new("\\\\.\\nul"));
#else
  return ts_value_string(ts_string_new("/dev/null"));
#endif
}

Value node_os_defaultEncoding(void) {
#ifdef _WIN32
  /* Get the active code page and map to encoding name */
  UINT codePage = GetACP();
  switch (codePage) {
    case 65001: return ts_value_string(ts_string_new("utf8"));
    case 936:   return ts_value_string(ts_string_new("gbk"));
    case 950:   return ts_value_string(ts_string_new("big5"));
    case 932:   return ts_value_string(ts_string_new("shift_jis"));
    case 949:   return ts_value_string(ts_string_new("euc-kr"));
    case 1252:  return ts_value_string(ts_string_new("latin1"));
    case 1250:  return ts_value_string(ts_string_new("windows-1250"));
    case 1251:  return ts_value_string(ts_string_new("windows-1251"));
    case 1253:  return ts_value_string(ts_string_new("windows-1253"));
    case 1254:  return ts_value_string(ts_string_new("windows-1254"));
    case 1255:  return ts_value_string(ts_string_new("windows-1255"));
    case 1256:  return ts_value_string(ts_string_new("windows-1256"));
    case 1257:  return ts_value_string(ts_string_new("windows-1257"));
    case 1258:  return ts_value_string(ts_string_new("windows-1258"));
    case 28591: return ts_value_string(ts_string_new("latin1"));
    case 28592: return ts_value_string(ts_string_new("iso-8859-2"));
    case 28593: return ts_value_string(ts_string_new("iso-8859-3"));
    case 28594: return ts_value_string(ts_string_new("iso-8859-4"));
    case 28595: return ts_value_string(ts_string_new("iso-8859-5"));
    case 28596: return ts_value_string(ts_string_new("iso-8859-6"));
    case 28597: return ts_value_string(ts_string_new("iso-8859-7"));
    case 28598: return ts_value_string(ts_string_new("iso-8859-8"));
    case 28605: return ts_value_string(ts_string_new("iso-8859-15"));
    default:    return ts_value_string(ts_string_new("utf8"));
  }
#else
  /* POSIX: check locale, default to utf8 */
  const char* lang = getenv("LANG");
  if (lang && lang[0]) {
    /* Extract encoding from LANG like "en_US.UTF-8" */
    const char* dot = strrchr(lang, '.');
    if (dot && dot[1]) {
      return ts_value_string(ts_string_new(dot + 1));
    }
  }
  return ts_value_string(ts_string_new("utf8"));
#endif
}

Value node_os_manufacturer(void) {
#ifdef _WIN32
  return wmi_query("Win32_ComputerSystem", "Manufacturer");
#else
  return dmi_read("sys_vendor");
#endif
}

Value node_os_model(void) {
#ifdef _WIN32
  return wmi_query("Win32_ComputerSystem", "Model");
#else
  return dmi_read("product_name");
#endif
}

Value node_os_serial(void) {
#ifdef _WIN32
  return wmi_query("Win32_BIOS", "SerialNumber");
#else
  return dmi_read("product_serial");
#endif
}

Value node_os_biosVersion(void) {
#ifdef _WIN32
  return wmi_query("Win32_BIOS", "SMBIOSBIOSVersion");
#else
  return dmi_read("bios_version");
#endif
}

Value node_os_biosReleaseDate(void) {
#ifdef _WIN32
  return wmi_query("Win32_BIOS", "ReleaseDate");
#else
  return dmi_read("bios_date");
#endif
}

/* ---------- GPU info ---------- */

#ifdef _WIN32
/* Get a single GPU property from WMI */
static Value gpu_wmi_query(const char* property, int index) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
    "powershell -NoProfile -Command \"$g = Get-CimInstance -ClassName Win32_VideoController; if ($g.Count -gt %d) { $g[%d].%s }\" 2>NUL",
    index, index, property);
  FILE* pipe = _popen(cmd, "r");
  if (!pipe) return ts_value_string(ts_string_new(""));
  char buf[512] = "";
  if (fgets(buf, sizeof(buf), pipe)) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
  }
  _pclose(pipe);
  return ts_value_string(ts_string_new(buf));
}

/* Get the number of GPUs */
static int gpu_count(void) {
  FILE* pipe = _popen("powershell -NoProfile -Command \"(Get-CimInstance -ClassName Win32_VideoController).Count\"", "r");
  if (!pipe) return 0;
  char buf[32] = "0";
  if (fgets(buf, sizeof(buf), pipe)) {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
  }
  _pclose(pipe);
  return atoi(buf);
}

/* Try to get GPU utilization via NVIDIA NVML (nvidia-smi) */
static double nvidia_get_utilization(void) {
  char buf[64] = "";
  /* Try PATH first */
  FILE* pipe = _popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  /* Fallback to full path */
  pipe = _popen("\"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe\" --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  return -1;
}

/* Try to get GPU temperature via nvidia-smi */
static double nvidia_get_temp(void) {
  char buf[64] = "";
  FILE* pipe = _popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  pipe = _popen("\"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe\" --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  return -1;
}

/* Get VRAM in MB from nvidia-smi (handles >4GB correctly) */
static double nvidia_get_vram_mb(void) {
  char buf[64] = "";
  FILE* pipe = _popen("nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  pipe = _popen("\"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvidia-smi.exe\" --query-gpu=memory.total --format=csv,noheader,nounits 2>NUL", "r");
  if (pipe && fgets(buf, sizeof(buf), pipe)) {
    _pclose(pipe);
    return atof(buf);
  }
  if (pipe) _pclose(pipe);
  return -1;
}
#endif

Value node_os_gpuInfo(void) {
  TSArray* arr = ts_array_new();

#ifdef _WIN32
  int count = gpu_count();
  for (int i = 0; i < count; i++) {
    TSHashMap* info = ts_hashmap_new();

    /* Name */
    Value name = gpu_wmi_query("Name", i);
    ts_hashmap_set(info, ts_string_new("name"), name);

    /* Vendor — infer from name */
    const char* nameStr = (name.tag == TAG_STRING && name.as.string) ? name.as.string->data : "";
    if (strstr(nameStr, "NVIDIA") || strstr(nameStr, "GeForce") || strstr(nameStr, "RTX") || strstr(nameStr, "GTX"))
      ts_hashmap_set(info, ts_string_new("vendor"), ts_value_string(ts_string_new("NVIDIA")));
    else if (strstr(nameStr, "AMD") || strstr(nameStr, "Radeon"))
      ts_hashmap_set(info, ts_string_new("vendor"), ts_value_string(ts_string_new("AMD")));
    else if (strstr(nameStr, "Intel"))
      ts_hashmap_set(info, ts_string_new("vendor"), ts_value_string(ts_string_new("Intel")));
    else
      ts_hashmap_set(info, ts_string_new("vendor"), ts_value_string(ts_string_new("unknown")));

    /* Memory — prefer nvidia-smi for NVIDIA (WMI AdapterRAM overflows >4GB) */
    double memMB = 0;
    int isNvidia = (strstr(nameStr, "NVIDIA") || strstr(nameStr, "GeForce") || strstr(nameStr, "RTX") || strstr(nameStr, "GTX"));
    if (isNvidia) {
      memMB = nvidia_get_vram_mb();
    }
    if (memMB <= 0) {
      /* Fallback to WMI AdapterRAM */
      Value memStr = gpu_wmi_query("AdapterRAM", i);
      if (memStr.tag == TAG_STRING && memStr.as.string) {
        memMB = atof(memStr.as.string->data) / (1024.0 * 1024.0);
      }
    }
    ts_hashmap_set(info, ts_string_new("memoryMB"), ts_value_number(memMB));

    /* Driver version */
    ts_hashmap_set(info, ts_string_new("driverVersion"), gpu_wmi_query("DriverVersion", i));

    /* Utilization — try nvidia-smi for NVIDIA GPUs */
    double utilization = -1;
    if (isNvidia) {
      utilization = nvidia_get_utilization();
    }
    ts_hashmap_set(info, ts_string_new("utilization"), ts_value_number(utilization));

    /* Temperature — try nvidia-smi */
    double temp = -1;
    if (isNvidia) {
      temp = nvidia_get_temp();
    }
    ts_hashmap_set(info, ts_string_new("temperature"), ts_value_number(temp));

    ts_array_push(arr, ts_value_object(info));
  }
#else
  /* Linux: enumerate /sys/class/drm/card* */
  char path[512];
  for (int card = 0; card < 16; card++) {
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/vendor", card);
    FILE* f = fopen(path, "r");
    if (!f) break;
    char vendor[32] = "";
    if (fgets(vendor, sizeof(vendor), f)) {
      size_t len = strlen(vendor);
      while (len > 0 && (vendor[len-1] == '\n' || vendor[len-1] == '\r')) vendor[--len] = '\0';
    }
    fclose(f);

    /* Read device name from uevent */
    snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/uevent", card);
    f = fopen(path, "r");
    char gpuName[256] = "unknown";
    if (f) {
      char line[512];
      while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PCI_ID=", 7) == 0) {
          char* colon = strchr(line + 7, ':');
          if (colon) {
            unsigned long ven = strtoul(line + 7, NULL, 16);
            if (ven == 0x10de) strcpy(gpuName, "NVIDIA GPU");
            else if (ven == 0x1002) strcpy(gpuName, "AMD GPU");
            else if (ven == 0x8086) strcpy(gpuName, "Intel GPU");
            else snprintf(gpuName, sizeof(gpuName), "GPU [%s]", line + 7);
          }
          break;
        }
      }
      fclose(f);
    }

    TSHashMap* info = ts_hashmap_new();
    ts_hashmap_set(info, ts_string_new("name"), ts_value_string(ts_string_new(gpuName)));
    ts_hashmap_set(info, ts_string_new("vendor"), ts_value_string(ts_string_new(vendor)));
    ts_hashmap_set(info, ts_string_new("memoryMB"), ts_value_number(0));
    ts_hashmap_set(info, ts_string_new("driverVersion"), ts_value_string(ts_string_new("")));
    ts_hashmap_set(info, ts_string_new("utilization"), ts_value_number(-1));
    ts_hashmap_set(info, ts_string_new("temperature"), ts_value_number(-1));
    ts_array_push(arr, ts_value_object(info));
  }
#endif

  return ts_value_array(arr);
}
