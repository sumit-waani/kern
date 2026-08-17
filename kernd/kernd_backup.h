/*
 * kernd_backup.h - Application backup system
 *
 * Creates tar.gz backups of application source directories and
 * provides pruning of old backups based on age.
 */

#ifndef KERND_BACKUP_H
#define KERND_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a backup of an application's source directory.
 * Archives <data_dir>/<app_name>/ into
 * <backup_dir>/<app_name>/<timestamp>.tar.gz using system tar.
 *
 * Returns 0 on success, -1 on failure.
 */
int kernd_backup_run(const char *app_name, const char *data_dir, const char *backup_dir);

/**
 * Prune old backups for an application.
 * Removes tar.gz files in <backup_dir>/<app_name>/ that are
 * older than max_age_days.
 *
 * Returns 0 on success, -1 on failure.
 */
int kernd_backup_prune(const char *app_name, const char *backup_dir, int max_age_days);

#ifdef __cplusplus
}
#endif

#endif /* KERND_BACKUP_H */
