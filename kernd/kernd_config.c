/*
 * kernd_config.c - Configuration loading for the kernd daemon
 *
 * Loads TOML configuration using libkern's kern_config_load() and
 * maps dotted keys to the kernd_config_t struct fields.
 */

#include "kernd_config.h"
#include "kern.h"

#include <stdlib.h>
#include <string.h>

kernd_config_t *kernd_config_defaults(void) {
    kernd_config_t *cfg = calloc(1, sizeof(kernd_config_t));
    if (!cfg) return NULL;

    cfg->admin_port = 8443;
    cfg->http_port = 80;
    cfg->https_port = 443;
    cfg->data_dir = strdup("/var/lib/kernd");
    cfg->log_dir = strdup("/var/log/kernd");
    cfg->dashboard_secret = NULL;
    cfg->admin_password_hash = NULL;

    if (!cfg->data_dir || !cfg->log_dir) {
        kernd_config_free(cfg);
        return NULL;
    }

    return cfg;
}

kernd_config_t *kernd_config_load(const char *path) {
    kernd_config_t *cfg = kernd_config_defaults();
    if (!cfg) return NULL;

    if (!path) return cfg;

    kern_config_t *raw = kern_config_load(path);
    if (!raw) return cfg;

    /* admin.port */
    int64_t admin_port = kern_config_get_int(raw, "admin.port");
    if (admin_port > 0 && admin_port < 65536) {
        cfg->admin_port = (int)admin_port;
    }

    /* http.port */
    int64_t http_port = kern_config_get_int(raw, "http.port");
    if (http_port > 0 && http_port < 65536) {
        cfg->http_port = (int)http_port;
    }

    /* https.port */
    int64_t https_port = kern_config_get_int(raw, "https.port");
    if (https_port > 0 && https_port < 65536) {
        cfg->https_port = (int)https_port;
    }

    /* paths.data_dir */
    const char *data_dir = kern_config_get_str(raw, "paths.data_dir");
    if (data_dir) {
        free(cfg->data_dir);
        cfg->data_dir = strdup(data_dir);
    }

    /* paths.log_dir */
    const char *log_dir = kern_config_get_str(raw, "paths.log_dir");
    if (log_dir) {
        free(cfg->log_dir);
        cfg->log_dir = strdup(log_dir);
    }

    /* security.dashboard_secret */
    const char *secret = kern_config_get_str(raw, "security.dashboard_secret");
    if (secret) {
        free(cfg->dashboard_secret);
        cfg->dashboard_secret = strdup(secret);
    }

    /* admin.password_hash */
    const char *pw_hash = kern_config_get_str(raw, "admin.password_hash");
    if (pw_hash) {
        free(cfg->admin_password_hash);
        cfg->admin_password_hash = strdup(pw_hash);
    }

    kern_config_free(raw);
    return cfg;
}

void kernd_config_free(kernd_config_t *cfg) {
    if (!cfg) return;
    free(cfg->data_dir);
    free(cfg->log_dir);
    free(cfg->dashboard_secret);
    free(cfg->admin_password_hash);
    free(cfg);
}
