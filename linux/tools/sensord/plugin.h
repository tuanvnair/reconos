/*
 * Copyright 2012 Daniel Borkmann <dborkma@tik.ee.ethz.ch>
 */

#ifndef PLUGIN_H
#define PLUGIN_H

#include <sys/time.h>
#include <stdint.h>

#include "notifier.h"

struct plugin_instance {
	char *name;			/* unique instance name */
	char *basename;			/* basename of plugin */
	void (*fetch)(struct plugin_instance *self);	/* callback function */
	double *cells;			/* fetch values */
	void *private_data;		/* plugin-private data */
	uint64_t schedule_int;		/* scheduling interval in us */
	uint64_t block_entries;		/* blocks in database */
	uint16_t cells_per_block;	/* cells per block in database */
	int slot;			/* table slot */
	int timedb_fd;			/* fd to backend storage */
	struct event_head pid_notifier;	/* threshold notifier */
};

#define MAX_PLUGINS		4096

#define TIME_IN_USEC(x)		(x)
#define TIME_IN_MSEC(x)		((x) * 1000)
#define TIME_IN_SEC(x)		((x) * 1000 * 1000)

extern void init_plugin(void);
extern void for_each_plugin(void (*fn)(struct plugin_instance *self));
extern int register_plugin_instance(struct plugin_instance *pi);
extern void unregister_plugin_instance(struct plugin_instance *pi);
extern struct plugin_instance *get_plugin_by_name(char *name);

#endif /* PLUGIN_H */
