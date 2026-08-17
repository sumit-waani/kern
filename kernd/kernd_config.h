/*
 * kernd_config.h - Configuration for the kernd daemon
 *
 * Defines the kernd_config_t struct and functions to load/free
 * configuration from a TOML file using libkern's config parser.
 */

#ifndef KERND_CONFIG_H
#define KERND_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int admin_port;             /* Default: 8443 */
    int http_port;              /* Default: 80 */
    int https_port;             /* Default: 443 */
    char *data_dir;             /* Default: /var/lib/kernd */
    char *log_dir;              /* Default: /var/log/kernd */
    char *dashboard_secret;     /* Random hex secret */
    char *admin_password_hash;  /* PBKDF2-HMAC-SHA256 hash */
} kernd_config_t;

/**
 * Load kernd configuration from a TOML file.
 * Falls back to defaults for any missing keys.
 * Returns NULL on allocation failure.
 */
kernd_config_t *kernd_config_load(const char *path);

/**
 * Create a config with all default values.
 * Returns NULL on allocation failure.
 */
kernd_config_t *kernd_config_defaults(void);

/**
 * Free a kernd config and all owned strings.
 */
void kernd_config_free(kernd_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* KERND_CONFIG_H */
