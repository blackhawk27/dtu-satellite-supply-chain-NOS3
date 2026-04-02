/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
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
 *  The CFS Memory Dwell (MD) Example Dwell Table 2
 */

/************************************************************************
** Includes
*************************************************************************/
#include "cfe.h"
#include "md_extern_typedefs.h"
#include "md_tbldefs.h"
#include "cfe_tbl_filedef.h"
#include "md_tblstruct.h"
#include "md_platform_cfg.h"

/*
** Dwell Table 2 — LC AppData baseline snapshot
** Dwell rate = sum(Delay) * SCH_wakeup_period = 4 * 1s = 4s per dwell packet.
** Reads three fields from LC_AppData_t (lc_app.h) for pre/post-attack comparison:
**
**   Offset 0: CmdCount    (uint16) — LC command counter
**   Offset 2: CmdErrCount (uint16) — LC error counter
**   Offset 4: APSampleCount (uint32) — total AP evaluation cycles
**
** WatchResults and ActionResults live in separate table allocations
** (LC_OperData.WRTPtr / ARTPtr) and are not directly addressable via LC_AppData.
** Use md_dw01.c Entry 1-2 (4-byte reads at offsets 0 and 4) for bulk LC state.
*/
MD_DwellTableLoad_t MD_Default_Dwell2_Tbl = {
    /* Enabled State */ MD_Dwell_States_ENABLED,
#if MD_INTERFACE_SIGNATURE_OPTION == 1
    /* Signature     */ "LCState-v1",
#endif
    /* Entry    Length  Delay  {Offset, SymName}                               */
    {
        /*  1 - LC CmdCount low byte (uint16 at offset 0 in LC_AppData_t) */
        {1, 1, {0, "LC_AppData"}},
        /*  2 - LC CmdErrCount low byte (uint16 at offset 2 in LC_AppData_t) */
        {1, 1, {2, "LC_AppData"}},
        /*  3 - LC APSampleCount (uint32 at offset 4 in LC_AppData_t) — FIXED: was duplicate of Entry 1 */
        {4, 1, {4, "LC_AppData"}},
        /*  4 - terminating wait */
        {0, 1, {0, ""}},
        /*  5-25 unused */
        /*   5 */ {0, 0, {0, ""}},
        /*   6 */ {0, 0, {0, ""}},
        /*   7 */ {0, 0, {0, ""}},
        /*   8 */ {0, 0, {0, ""}},
        /*   9 */ {0, 0, {0, ""}},
        /*  10 */ {0, 0, {0, ""}},
        /*  11 */ {0, 0, {0, ""}},
        /*  12 */ {0, 0, {0, ""}},
        /*  13 */ {0, 0, {0, ""}},
        /*  14 */ {0, 0, {0, ""}},
        /*  15 */ {0, 0, {0, ""}},
        /*  16 */ {0, 0, {0, ""}},
        /*  17 */ {0, 0, {0, ""}},
        /*  18 */ {0, 0, {0, ""}},
        /*  19 */ {0, 0, {0, ""}},
        /*  20 */ {0, 0, {0, ""}},
        /*  21 */ {0, 0, {0, ""}},
        /*  22 */ {0, 0, {0, ""}},
        /*  23 */ {0, 0, {0, ""}},
        /*  24 */ {0, 0, {0, ""}},
        /*  25 */ {0, 0, {0, ""}},
    }};

CFE_TBL_FILEDEF(MD_Default_Dwell2_Tbl, MD.DWELL_TABLE2, MD Dwell Table 2, md_dw02.tbl)
