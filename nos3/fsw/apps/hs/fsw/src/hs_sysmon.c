/************************************************************************
 * NASA Docket No. GSC-18,920-1, and identified as “Core Flight
 * System (cFS) Health & Safety (HS) Application version 2.4.1”
 *
 * Copyright (c) 2021 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/**
 * @file
 *   Health and Safety (HS) application system monitor component.
 *
 * NOS3 simulation implementation: reads CPU utilization from Linux /proc/stat
 * instead of the CFE_PSP_IODriver hardware interface (which does not exist in
 * the NOS3 POSIX PSP).  Produces the same numeric output expected by HS
 * (scaled to HS_CPU_UTILIZATION_MAX) and also writes a structured log line
 * that Logstash/Kibana can parse from cfs_evs.log.
 */

/*************************************************************************
** Includes
*************************************************************************/
#include "hs_sysmon.h"
#include "hs_internal_cfg.h"
#include "hs_app.h"
#include "cfe.h"
#include <stdio.h>

/* Persistent state for delta-based /proc/stat measurement */
static unsigned long long g_prev_idle  = 0;
static unsigned long long g_prev_total = 0;
static int32              g_util_peak  = 0;

/* Read one /proc/stat cpu line into the six fields we need */
static int ReadProcStat(unsigned long long *idle_out, unsigned long long *total_out)
{
    FILE              *fp;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

    fp = fopen("/proc/stat", "r");
    if (fp == NULL)
        return -1;

    if (fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8)
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    *idle_out  = idle + iowait;
    *total_out = user + nice + system + idle + iowait + irq + softirq + steal;
    return 0;
}

CFE_Status_t HS_SysMonInit(void)
{
    /* Seed previous values so the first GetCpuUtilization call returns a valid delta */
    ReadProcStat(&g_prev_idle, &g_prev_total);
    g_util_peak = 0;
    CFE_ES_WriteToSysLog("HS_SYSMON: Initialized - reading CPU utilization from /proc/stat\n");
    return CFE_SUCCESS;
}

void HS_SysMonCleanup(void)
{
    g_prev_idle  = 0;
    g_prev_total = 0;
    g_util_peak  = 0;
}

CFE_Status_t HS_SysMonGetCpuUtilization(void)
{
    unsigned long long curr_idle, curr_total, delta_idle, delta_total;
    int32              util;
    int                cpu_pct;

    if (ReadProcStat(&curr_idle, &curr_total) != 0)
        return -1;

    /* Guard against wrap or first-call edge cases */
    if (g_prev_total == 0 || curr_total <= g_prev_total)
    {
        g_prev_idle  = curr_idle;
        g_prev_total = curr_total;
        return 0;
    }

    delta_idle  = curr_idle  - g_prev_idle;
    delta_total = curr_total - g_prev_total;
    g_prev_idle  = curr_idle;
    g_prev_total = curr_total;

    /* Scale busy time to HS_CPU_UTILIZATION_MAX (10000 = 100%) */
    util = (int32)(((delta_total - delta_idle) * (unsigned long long)HS_CPU_UTILIZATION_MAX) / delta_total);
    if (util < 0)                    util = 0;
    if (util > HS_CPU_UTILIZATION_MAX) util = HS_CPU_UTILIZATION_MAX;

    if (util > g_util_peak)
        g_util_peak = util;

    /* Integer percentage (0-100) for human-readable logging */
    cpu_pct = (int)(((delta_total - delta_idle) * 100ULL) / delta_total);

    /*
     * Structured log line captured by cfs_evs_capture.sh into omni_logs/cfs_evs.log.
     * Logstash parses this into hs_cpu_pct / hs_cpu_util_scaled / hs_cpu_util_peak.
     */
    CFE_ES_WriteToSysLog("HS_SYSMON cpu_pct=%d util_scaled=%ld util_peak=%ld\n",
                         cpu_pct, (long)util, (long)g_util_peak);

    return util;
}
