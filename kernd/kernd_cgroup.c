/*
 * kernd_cgroup.c - cgroup v2 resource management implementation
 *
 * Creates per-app cgroup directories under /sys/fs/cgroup/kernd/
 * and writes resource limits. Gracefully handles cases where
 * cgroup v2 is not available (not root, fs not mounted).
 */

#include "kernd_cgroup.h"
#include "kernd_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CGROUP_BASE "/sys/fs/cgroup"
#define CGROUP_KERND "/sys/fs/cgroup/kernd"

static bool cgroup_enabled = false;

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        kernd_log_warn("cgroup: cannot write %s: %s", path, strerror(errno));
        return -1;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

int kernd_cgroup_init(void) {
    struct stat st;

    /* Check if cgroup v2 filesystem is mounted */
    if (stat(CGROUP_BASE, &st) != 0 || !S_ISDIR(st.st_mode)) {
        kernd_log_warn("cgroup: %s not available, resource limits disabled", CGROUP_BASE);
        cgroup_enabled = false;
        return -1;
    }

    /* Try to create the kernd cgroup directory */
    if (mkdir(CGROUP_KERND, 0755) != 0 && errno != EEXIST) {
        kernd_log_warn("cgroup: cannot create %s: %s (resource limits disabled)",
                       CGROUP_KERND, strerror(errno));
        cgroup_enabled = false;
        return -1;
    }

    /* Verify we can write to it */
    char test_path[512];
    snprintf(test_path, sizeof(test_path), "%s/cgroup.procs", CGROUP_KERND);
    if (access(test_path, W_OK) != 0) {
        kernd_log_warn("cgroup: %s not writable, resource limits disabled", CGROUP_KERND);
        cgroup_enabled = false;
        return -1;
    }

    cgroup_enabled = true;
    kernd_log_info("cgroup: initialized at %s", CGROUP_KERND);
    return 0;
}

bool kernd_cgroup_available(void) {
    return cgroup_enabled;
}

int kernd_cgroup_create(const char *app_name, int cpu_percent, int mem_mb, int max_pids) {
    if (!app_name || !cgroup_enabled) {
        return -1;
    }

    if (cpu_percent < 1 || cpu_percent > 100 || mem_mb < 1 || max_pids < 1) {
        return -1;
    }

    /* Create app cgroup directory */
    char cgroup_path[512];
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_KERND, app_name);

    if (mkdir(cgroup_path, 0755) != 0 && errno != EEXIST) {
        kernd_log_error("cgroup: cannot create %s: %s", cgroup_path, strerror(errno));
        return -1;
    }

    /* Write memory.max */
    char file_path[1024];
    char value[64];

    snprintf(file_path, sizeof(file_path), "%s/memory.max", cgroup_path);
    snprintf(value, sizeof(value), "%lld", (long long)mem_mb * 1024 * 1024);
    if (write_file(file_path, value) != 0) {
        return -1;
    }

    /* Write cpu.max (microseconds per 100ms period) */
    snprintf(file_path, sizeof(file_path), "%s/cpu.max", cgroup_path);
    int cpu_us = cpu_percent * 1000;  /* e.g., 50% = 50000 us per 100000 us period */
    snprintf(value, sizeof(value), "%d 100000", cpu_us);
    if (write_file(file_path, value) != 0) {
        return -1;
    }

    /* Write pids.max */
    snprintf(file_path, sizeof(file_path), "%s/pids.max", cgroup_path);
    snprintf(value, sizeof(value), "%d", max_pids);
    if (write_file(file_path, value) != 0) {
        return -1;
    }

    kernd_log_info("cgroup: created %s (cpu=%d%%, mem=%dMB, pids=%d)",
                   app_name, cpu_percent, mem_mb, max_pids);
    return 0;
}

int kernd_cgroup_assign(const char *app_name, pid_t pid) {
    if (!app_name || !cgroup_enabled || pid <= 0) {
        return -1;
    }

    char file_path[1024];
    char value[32];

    snprintf(file_path, sizeof(file_path), "%s/%s/cgroup.procs", CGROUP_KERND, app_name);
    snprintf(value, sizeof(value), "%d", (int)pid);

    if (write_file(file_path, value) != 0) {
        return -1;
    }

    kernd_log_info("cgroup: assigned pid %d to %s", (int)pid, app_name);
    return 0;
}

int kernd_cgroup_destroy(const char *app_name) {
    if (!app_name || !cgroup_enabled) {
        return -1;
    }

    char cgroup_path[512];
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_KERND, app_name);

    /* rmdir only works if the cgroup has no processes */
    if (rmdir(cgroup_path) != 0) {
        kernd_log_warn("cgroup: cannot remove %s: %s", cgroup_path, strerror(errno));
        return -1;
    }

    kernd_log_info("cgroup: destroyed %s", app_name);
    return 0;
}
