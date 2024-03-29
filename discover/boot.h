#ifndef _BOOT_H
#define _BOOT_H

struct boot_option;
struct boot_command;

typedef void (*boot_status_fn)(void *arg, struct boot_status *);

int boot(void *ctx, struct discover_boot_option *opt, struct boot_command *cmd,
		int dry_run, boot_status_fn status_fn, void *status_arg);

#endif /* _BOOT_H */
