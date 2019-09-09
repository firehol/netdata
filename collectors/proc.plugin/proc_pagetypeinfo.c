// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_proc.h"

// For PAGE_SIZE
#include <sys/user.h>


#define PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME "/proc/pagetypeinfo"
#define CONFIG_SECTION_PLUGIN_PROC_PAGETYPEINFO "plugin:" PLUGIN_PROC_CONFIG_NAME ":" PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME

// Zone struct is pglist_data, in include/linux/mmzone.h
// MAX_NR_ZONES is from __MAX_NR_ZONE, which is the last value of the enum.
#define MAX_PAGETYPE_ORDER 11

// Names are in mm/page_alloc.c :: migratetype_names. Max size = 10.
#define MAX_ZONETYPE_NAME 16
#define MAX_PAGETYPE_NAME 16

// Defined in include/linux/mmzone.h as __MAX_NR_ZONE (last enum of zone_type)
#define MAX_ZONETYPE  6
// Defined in include/linux/mmzone.h as MIGRATE_TYPES (last enum of migratetype)
#define MAX_PAGETYPE  7

// Defined as 2^CONFIG_cnt_nodes_SHIFT. We'll use 16 for now (8 max * 2 as margin):
// - Up to 4 Intel Xeon, each with SNC (x2) = 8 cnt_nodes.
// - Up to 2 AMD  EPYC, each with 4 CPUs = 8 cnt_nodes.
#define MAX_NUMA_cnt_nodes 16

//
// /proc/pagetypeinfo is declared in mm/vmstat.c :: init_mm_internals
//
struct pageline {
	int node;
	char *zone;
	char *type;
	uint64_t free_pages[MAX_PAGETYPE_ORDER];
};

struct nodezone {
	int node;
	char *zone;
	struct pageline* lines[MAX_PAGETYPE];
};

struct systemorder {
	uint64_t count;
	RRDDIM *rd;
};



static inline uint64_t pageline_total_size(struct pageline *p) {
	uint64_t sum = 0;
	for (int o=0; o<MAX_PAGETYPE_ORDER; o++)
		sum += p->free_pages[o] * (o+1) * PAGE_SIZE;
	return sum;
}

static inline uint64_t pageline_total_count(struct pageline *p) {
	uint64_t sum = 0;
	for (int o=0; o<MAX_PAGETYPE_ORDER; o++)
		sum += p->free_pages[0];
	return sum;
}



int do_proc_pagetypeinfo(int update_every, usec_t dt) {
	(void)dt;

	static int cnt_nodes = -1, cnt_zones = -1, cnt_pagetypes = -1, cnt_pageorders = -1, linemax = -1;

	static struct systemorder systemorders[MAX_PAGETYPE_ORDER] = {};
	static struct pageline** pagelines = NULL;
	static size_t pagelines_cnt = -1;

	static procfile *ff = NULL;

	static RRDSET *st_order = NULL;
	//static RRDSET **st_nodezone = NULL;
	static RRDSET **st_nodezonetype = NULL;

	size_t l, o, p;
	struct pageline *pgl;

	if(unlikely(!ff)) {
		ff = procfile_open(PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME, " \t,", PROCFILE_FLAG_DEFAULT);
	}
	if(unlikely(!ff))
		return 1;

	ff = procfile_readall(ff);
	if(unlikely(!ff))
		return 0; // we return 0, so that we will retry to open it next time

	// --------------------------------------------------------------------
	// Init: find how many cnt_nodes, Zones and Types
	if(unlikely(cnt_nodes == -1)) {
		size_t nodelast = -1, tmpint = -1;

		size_t lines = procfile_lines(ff);
		if(unlikely(!lines)) {
			error("PLUGIN: PROC_PAGETYPEINFO: Cannot read %s, zero lines reported.", PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME);
			return 1;
		}

		// 4th line is the "Free pages count...". Just substract the 8 words.
		cnt_pageorders = procfile_linewords(ff, 3) - 8;
		cnt_nodes = 0;
		pagelines_cnt = 0;

		for (l=4; l < lines; l++) {
			// first empty line is the end of the "Free pages" block
			if (strncmp(procfile_lineword(ff, l, 0), "Node", 4) != 0) {
				linemax = l;
				break;
			}

			// Count the number of numa cnt_nodes
			if( (tmpint = strtoul(procfile_lineword(ff, l, 0), NULL, 16) ) && tmpint != nodelast ) {
				cnt_nodes++;
				nodelast = tmpint;
			}

			// Unmovable is always the first in the enum. The first line higher than 4
			if (strncmp(procfile_lineword(ff, l, 6), "Unmovable", 10) == 0 && l >4) {
				if (cnt_pagetypes == -1)
					cnt_pagetypes = l - 4;
			}

			pagelines_cnt++;
		}

		debug(0x1, "Init: cnt_nodes=%d types=%d orders=%d linemax=%d",
			cnt_nodes, cnt_pagetypes, cnt_pageorders, linemax );

		// Init pagelines
		if (!pagelines) {
			pagelines = callocz(pagelines_cnt, sizeof(struct pageline));
			if (!pagelines) {
				error("PLUGIN: PROC_PAGETYPEINFO: Cannot allocate %lu B for pageline", linemax * sizeof(struct pageline));
				return 1;
			}
		}

		// Init the RRD graphs

		// Per-Order: sum of all node, zone, type Grouped by order
		st_order = rrdset_create_localhost(
			"mem"
			, "pagetype_orders"
			, NULL
			, "pagetype"
			, NULL
			, "System orders available"
			, "MB"
			, PLUGIN_PROC_NAME
			, PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME
			, NETDATA_CHART_PRIO_SYSTEM_MEMFRAG
			, update_every
			, RRDSET_TYPE_STACKED
		);
		for (o = 0; o < MAX_PAGETYPE_ORDER; o++) {
			char id[3+1];
			snprintfz(id, 3, "%lu", o);
			char name[20+1];
			snprintfz(name, 16,"Order %lu (%luMB)", o, (1 << o) * PAGE_SIZE);
			systemorders[o].rd = rrddim_add(st_order, id, name, 1, 1, RRD_ALGORITHM_ABSOLUTE);
			rrddim_set_by_pointer(st_order, systemorders[o].rd, systemorders[o].count);
		}

		// Per Numa-Node & Zone
		// TODO

		// Per-Numa Node & Zone & Type (full detail). Only if sum(line) > 0
		st_nodezonetype = callocz(cnt_zones, sizeof(RRDSET*));
		for (p = 0; p < pagelines_cnt; p++) {
			pgl = pagelines[p];

			// Skip empty pagelines
			if (pageline_total_count(pgl) == 0)
				continue;


			char id[MAX_ZONETYPE_NAME+1];
			snprintfz(id, MAX_ZONETYPE_NAME, "node%d_%s_%s", pgl->node, pgl->zone, pgl->type);

			st_nodezonetype[p] = rrdset_create_localhost(
					"mem"
					, id
					, NULL
					, "pagetype"
					, NULL
					, "Page Size Distribution"
					, "pages size"
					, PLUGIN_PROC_NAME
					, PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME
					, NETDATA_CHART_PRIO_MEM_PAGEFRAG
					, update_every
					, RRDSET_TYPE_STACKED
			);

			for (o = 0; o < MAX_PAGETYPE_ORDER; o++) {
				char id[3+1];
				snprintfz(id, 3, "%lu", o);
				char name[20+1];
				snprintfz(name, 20,"Order %lu (%luMB)", o, (1 << o) * PAGE_SIZE);
				RRDDIM *rd = rrddim_add(st_nodezonetype[p], id, name, 1, 1, RRD_ALGORITHM_ABSOLUTE);
				rrddim_set_by_pointer(st_order, rd, pgl->free_pages[o]);
			}
		}
	}

	if(unlikely(!cnt_nodes)) {
		error("PLUGIN: PROC_PAGETPEINFO: Cannot find the number of CPUs in %s", PLUGIN_PROC_MODULE_PAGETYPEINFO_NAME);
		return 1;
	}

	// --------------------------------------------------------------------
	// Update pagelines

	// Process each line
	for (p = 0; p < pagelines_cnt; p++) {
		l = p+4;

		int words = procfile_linewords(ff, l);
		if (words < 6+cnt_pageorders) {
			error("Unable to read line %lu, only %d words found instead of %d", l, words, 6 + cnt_pageorders);
			break;
		}

		for (o = 0; o < MAX_PAGETYPE_ORDER; o++) {
			// Reset counter
			if (p == 0)
				systemorders[o].count = 0;

			// Update orders of the current line
			pagelines[p]->free_pages[o] = str2uint64_t(procfile_lineword(ff, l, o+6));

			// Update sum by order
			systemorders[o].count += pagelines[p]->free_pages[o];
		}
	}

	// --------------------------------------------------------------------
	// update RRD values

	// Global system per order
	rrdset_next(st_order);
	for (o = 0; o < MAX_PAGETYPE_ORDER; o++) {
		rrddim_set_by_pointer(st_order, systemorders[o].rd, systemorders[o].count);
	}
	rrdset_done(st_order);


	// Per Node-Zone-Type

	return 0;
}
