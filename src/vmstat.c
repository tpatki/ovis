/*
 * Copyright (c) 2010 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2010 Sandia Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

/**
 * \file vmstat.c
 * \brief /proc/vmstat data provider
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include "ldms.h"
#include "ldmsd.h"
#include <asm-x86_64/unistd.h>

#define PROC_FILE "/proc/vmstat"

static char *procfile = PROC_FILE;

ldms_set_t set;
FILE *mf;
ldms_metric_t *metric_table;
ldmsd_msg_log_f msglog;
ldms_metric_t compid_metric_handle;
union ldms_value comp_id;
ldms_metric_t counter_metric_handle;
ldms_metric_t pid_metric_handle;
ldms_metric_t tid_metric_handle;
static uint64_t counter;
static uint64_t mypid;
static uint64_t mytid;

static ldms_set_t get_set()
{
	return set;
}

static int create_metric_set(const char *path)
{
	size_t meta_sz, tot_meta_sz;
	size_t data_sz, tot_data_sz;
	int rc, metric_count;
	uint64_t metric_value;
	char *s;
	char lbuf[256];
	char metric_name[128];

	mf = fopen(procfile, "r");
	if (!mf) {
		msglog("Could not open the vmstat file '%s'...exiting\n", procfile);
		return ENOENT;
	}

	rc = ldms_get_metric_size("component_id", LDMS_V_U64,
				  &tot_meta_sz, &tot_data_sz);

	//counter
	rc = ldms_get_metric_size("counter", LDMS_V_U64, &meta_sz, &data_sz);
	tot_meta_sz += meta_sz;
	tot_data_sz += data_sz;

        //and add the pid
        rc = ldms_get_metric_size("pid", LDMS_V_U64, &meta_sz, &data_sz);
        tot_meta_sz += meta_sz;
        tot_data_sz += data_sz;

        //and add the tid
        rc = ldms_get_metric_size("tid", LDMS_V_U64, &meta_sz, &data_sz);
        tot_meta_sz += meta_sz;
        tot_data_sz += data_sz;

	/*
	 * Process the file once first to determine the metric set size.
	 */
	metric_count = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &metric_value);
		if (rc < 2)
			break;

		rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc)
			return rc;

		tot_meta_sz += meta_sz;
		tot_data_sz += data_sz;
		metric_count++;
	} while (s);


	/* Create the metric set */
	rc = ldms_create_set(path, tot_meta_sz, tot_data_sz, &set);
	if (rc)
		return rc;

	metric_table = calloc(metric_count, sizeof(ldms_metric_t));
	if (!metric_table)
		goto err;

	/*
	 * Process the file again to define all the metrics.
	 */
	compid_metric_handle = ldms_add_metric(set, "component_id", LDMS_V_U64);
	if (!compid_metric_handle) {
		rc = ENOMEM;
		goto err;
	} //compid set in config


        //and add the counter
	counter_metric_handle = ldms_add_metric(set, "counter", LDMS_V_U64);
	if (!counter_metric_handle) {
		rc = ENOMEM;
		goto err;
	} //counter set in config

        //and add the pid
        pid_metric_handle = ldms_add_metric(set, "pid", LDMS_V_U64);
        if (!pid_metric_handle) {
                rc = ENOMEM;
                goto err;
        }

        //and add the tid
        tid_metric_handle = ldms_add_metric(set, "tid", LDMS_V_U64);
        if (!tid_metric_handle) {
                rc = ENOMEM;
                goto err;
        }

	//counter
	counter = 0;
	union ldms_value v;
	v.v_u64 = counter;
	ldms_set_metric(counter_metric_handle, &v);

        //also set the pid
	mypid=getpid();
	v.v_u64 = mypid;
	ldms_set_metric(pid_metric_handle, &v);

        //also set the tid
	mytid = syscall(__NR_gettid);
	v.v_u64 = mytid;
	ldms_set_metric(tid_metric_handle, &v);

	int metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &metric_value);
		if (rc < 2)
			break;

		rc = ldms_get_metric_size(metric_name, LDMS_V_U64, &meta_sz, &data_sz);
		if (rc)
			return rc;

		metric_table[metric_no] = ldms_add_metric(set, metric_name, LDMS_V_U64);

		if (!metric_table[metric_no]){
			rc = ENOMEM;
			goto err;
		}
		metric_no++;
	} while (s);

	return 0;

 err:
	ldms_set_release(set);
	return rc;
}

static int config(struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;

	value = av_value(avl, "component_id");
	if (value)
		comp_id.v_u64 = strtol(value, NULL, 0);
	
	value = av_value(avl, "set");
	if (value)
		create_metric_set(value);

	return 0;
}

static int sample(void)
{
	int rc;
	int metric_no;
	char *s;
	char lbuf[256];
	char metric_name[128];
	union ldms_value v;
        v.v_u64=getpid();
        ldms_set_metric(pid_metric_handle, &v);

	if (!set) {
		msglog("vmstat: plugin not initialized\n");
		return EINVAL;
	}

	ldms_set_metric(compid_metric_handle, &comp_id);

        v.v_u64 = syscall(__NR_gettid);
        ldms_set_metric(tid_metric_handle, &v);
	v.v_u64 = ++counter;
	ldms_set_metric(counter_metric_handle, &v);
	metric_no = 0;
	fseek(mf, 0, SEEK_SET);
	do {
		s = fgets(lbuf, sizeof(lbuf), mf);
		if (!s)
			break;
		rc = sscanf(lbuf, "%s %" PRIu64 "\n", metric_name, &v.v_u64);
		if (rc != 2)
			return EINVAL;

		ldms_set_metric(metric_table[metric_no], &v);
		metric_no++;
	} while (s);
	return 0;
}

static void term(void)
{
	if (set)
		ldms_destroy_set(set);
	set = NULL;
}


static struct ldmsd_sampler vmstat_plugin = {
	.base = {
		.name = "vmstat",
		.term = term,
		.config = config,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	return &vmstat_plugin.base;
}
