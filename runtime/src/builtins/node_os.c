#include "node_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
#endif

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
    ts_array_push(arr, ts_value_object(cpu));
  }

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
