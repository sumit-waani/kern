/*
 * kernd_cgroup.h - cgroup v2 resource management
 *
 * Creates per-app cgroups under /sys/fs/cgroup/kernd/ to enforce
 * CPU, memory, and PID limits. Gracefully falls back to a no-op
 * when cgroup v2 is unavailable (not root, fs not mounted, etc.).
 */

#ifndef KERND_CGROUP_H
#define KERND_CGROUP_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize cgroup subsystem.
 * Checks if /sys/fs/cgroup is available and writable.
 * Returns 0 if cgroups are functional, -1 if not available.
 * Logs a warning on failure but does not terminate.
 */
int kernd_cgroup_init(void);

/**
 * Check whether cgroup support is available.
 * Returns true if kernd_cgroup_init() succeeded.
 */
bool kernd_cgroup_available(void);

/**
 * Create a cgroup for the given application with resource limits.
 * Creates /sys/fs/cgroup/kernd/<app_name>/ and writes limits.
 * cpu_percent: 1-100 (percentage of one CPU core)
 * mem_mb: memory limit in megabytes
 * max_pids: maximum number of processes
 * Returns 0 on success, -1 on failure or if cgroups unavailable.
 */
int kernd_cgroup_create(const char *app_name, int cpu_percent, int mem_mb, int max_pids);

/**
 * Assign a process to an app's cgroup.
 * Writes the PID to /sys/fs/cgroup/kernd/<app_name>/cgroup.procs.
 * Returns 0 on success, -1 on failure or if cgroups unavailable.
 */
int kernd_cgroup_assign(const char *app_name, pid_t pid);

/**
 * Destroy an app's cgroup directory.
 * Returns 0 on success, -1 on failure or if cgroups unavailable.
 */
int kernd_cgroup_destroy(const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* KERND_CGROUP_H */
