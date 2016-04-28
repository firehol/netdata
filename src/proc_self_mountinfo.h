#include "procfile.h"

#ifndef NETDATA_PROC_SELF_MOUNTINFO_H
#define NETDATA_PROC_SELF_MOUNTINFO_H 1

struct mountinfo {
	long id;                // mount ID: unique identifier of the mount (may be reused after umount(2)).
	long parentid;          // parent ID: ID of parent mount (or of self for the top of the mount tree).
	unsigned long major;    // major:minor: value of st_dev for files on filesystem (see stat(2)).
	unsigned long minor;

	char *root;             // root: root of the mount within the filesystem.
	char *mount_point;      // mount point: mount point relative to the process's root.
	char *mount_options;    // mount options: per-mount options.

	int optional_fields_count;
	char **optional_fields; // optional fields: zero or more fields of the form "tag[:value]".

	char *filesystem;       // filesystem type: name of filesystem in the form "type[.subtype]".
	char *mount_source;     // mount source: filesystem-specific information or "none".
	char *super_options;    // super options: per-superblock options.

	struct mountinfo *next;
};

extern struct mountinfo *mountinfo_find(struct mountinfo *root, unsigned long major, unsigned long minor);
extern void mountinfo_free(struct mountinfo *mi);
extern struct mountinfo *mountinfo_read();

#endif /* NETDATA_PROC_SELF_MOUNTINFO_H */