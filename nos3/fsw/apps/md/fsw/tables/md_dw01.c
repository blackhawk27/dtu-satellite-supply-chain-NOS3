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
 *  The CFS Memory Dwell (MD) Example Dwell Table #1
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
** Dwell Table 1 — System Health Monitor
** Dwell rate = sum(Delay) * SCH_wakeup_period = 5 * 1s = 5s per dwell packet.
**
** Entries use OSAL symbol table resolution: SymName is a global C symbol name,
** Offset is the byte offset within that symbol's structure.
** Length is bytes to dwell: 1 (uint8), 2 (uint16), 4 (uint32/float).
** A zero-length entry with Delay > 0 is a pure wait step.
** The table terminates at the first entry with Length=0 AND Delay=0.
**
** Symbol names below resolve at runtime — verify names with:
**   nm <cfs_binary> | grep -i "LC_AppData\|SC_OperData\|CFE_ES_Global"
*/
MD_DwellTableLoad_t MD_Default_Dwell1_Tbl = {
    /* Enabled State */ MD_Dwell_States_ENABLED,
#if MD_INTERFACE_SIGNATURE_OPTION == 1
    /* Signature     */ "SysHealth-v1",
#endif
    /* Entry    Length  Delay  {Offset, SymName}                                        */
    {
        /*  1 - LC WP results count (uint32) - how many WPs are currently failing */
        {4, 1, {0,   "LC_AppData"}},
        /*  2 - LC AP results bitmap (uint32) - which APs have fired */
        {4, 1, {4,   "LC_AppData"}},
        /*  3 - SC ATS state byte (uint8 padded to uint32 read) */
        {4, 1, {0,   "SC_OperData"}},
        /*  4 - ES registered app count (uint32) */
        {4, 1, {0,   "CFE_ES_Global"}},
        /*  5 - terminating wait (delay only, no data) */
        {0, 1, {0,   ""}},
        /*  6-25 unused */
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

CFE_TBL_FILEDEF(MD_Default_Dwell1_Tbl, MD.DWELL_TABLE1, MD Dwell Table 1, md_dw01.tbl)
