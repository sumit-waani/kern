/*
 * kernd_init.h - 'kernd init' subcommand
 *
 * Generates initial configuration with random secrets and writes
 * the dashboard.toml config file.
 */

#ifndef KERND_INIT_H
#define KERND_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the 'kernd init' subcommand.
 * Generates a 32-byte random hex secret and a 16-byte random admin
 * password (base64url-encoded), hashes the password, and writes config
 * to the given path (default: /etc/kernd/dashboard.toml).
 *
 * Returns 0 on success, -1 on failure.
 */
int kernd_init_run(const char *config_path);

#ifdef __cplusplus
}
#endif

#endif /* KERND_INIT_H */
