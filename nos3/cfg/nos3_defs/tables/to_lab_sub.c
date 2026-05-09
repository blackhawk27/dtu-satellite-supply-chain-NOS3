/************************************************************************
**
**      GSC-18128-1, "Core Flight Executive Version 6.7"
**
**      Copyright (c) 2006-2002 United States Government as represented by
**      the Administrator of the National Aeronautics and Space Administration.
**      All Rights Reserved.
**
**      Licensed under the Apache License, Version 2.0 (the "License");
**      you may not use this file except in compliance with the License.
**      You may obtain a copy of the License at
**
**        http://www.apache.org/licenses/LICENSE-2.0
**
**      Unless required by applicable law or agreed to in writing, software
**      distributed under the License is distributed on an "AS IS" BASIS,
**      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**      See the License for the specific language governing permissions and
**      limitations under the License.
**
** File: to_lab_sub_table.c
**
** Purpose:
**  Define TO Lab CPU specific subscription table
**
** Notes:
**
*************************************************************************/

/*
** Include Files
*/
#include "cfe_tbl_filedef.h"  /* Required to obtain the CFE_TBL_FILEDEF macro definition */
#include "to_lab_sub_table.h"
#include "to_lab_msgids.h"
#include "ci_lab_msgids.h"
#include "cs_msgids.h"
#include "ci_msgids.h"
#include "cf_msgids.h"
#include "ds_msgids.h"
#include "fm_msgids.h"
#include "hs_msgids.h"
#include "hk_msgids.h"
#include "lc_msgids.h"
#include "sc_msgids.h"
#include "sch_msgids.h"
#include "to_msgids.h"
#include "md_msgids.h"
#include "mm_msgids.h"
#include "sbn_msgids.h"
#include "beacon_app_msgids.h"

/*
** Component Include Files
*/
#include "cam_msgids.h"
#include "generic_css_msgids.h"
#include "generic_eps_msgids.h"
#include "generic_fss_msgids.h"
#include "generic_imu_msgids.h"
#include "generic_mag_msgids.h"
#include "generic_radio_msgids.h"
#include "generic_reaction_wheel_msgids.h"
#include "generic_torquer_msgids.h"
#include "generic_thruster_msgids.h"
#include "novatel_oem615_msgids.h"
#include "sample_msgids.h"
#include "generic_adcs_msgids.h"
#include "generic_star_tracker_msgids.h"
#include "mgr_msgids.h"
#include "syn_msgids.h"
#include "generic_tt_c_msgids.h"
#include "generic_gnss_msgids.h"

/*
** Local Structure Declarations
*/
#define CF_CONFIG_TLM_MID 0x08B2
#define CF_PDU_TLM_MID    0x0FFD

/*
TO_LAB_Subs_t TO_LAB_Subs =
{
    .Subs =
    {
         CFS App Subscriptions 
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_DATA_TYPES_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CI_LAB_HK_TLM_MID), {0, 0}, 4},

         Add these if needed 
        {CFE_SB_MSGID_WRAP_VALUE(CF_CONFIG_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_HK_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_HK_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_DIR_LIST_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SC_HK_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(DS_HK_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(LC_HK_TLM_MID), {0,0}, 4},

         cFE Core subscriptions 
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_DIAG_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_STATS_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_REG_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_LONG_EVENT_MSG_MID), {0, 0}, 32},

         Component Specifics 
        {CFE_SB_MSGID_WRAP_VALUE(CAM_HK_TLM_MID),               {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(CAM_EXP_TLM_MID),              {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_HK_TLM_MID),       {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_DEVICE_TLM_MID),   {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_EPS_HK_TLM_MID),       {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_HK_TLM_MID),       {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_DEVICE_TLM_MID),   {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_HK_TLM_MID),       {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_DEVICE_TLM_MID),   {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_HK_TLM_MID),       {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_DEVICE_TLM_MID),   {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RADIO_HK_TLM_MID),     {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RW_APP_HK_TLM_MID),    {0,0},  32},
	    {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TORQUER_HK_TLM_MID),   {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(NOVATEL_OEM615_HK_TLM_MID),    {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(NOVATEL_OEM615_DEVICE_TLM_MID),{0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_HK_TLM_MID),            {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_DEVICE_TLM_MID),        {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_HK_TLM_MID),      {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_DI_MID),          {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_AD_MID),          {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_GNC_MID),         {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_AC_MID),          {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_DO_MID),          {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_HK_TLM_MID),{0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_DEVICE_TLM_MID),{0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_THRUSTER_HK_TLM_MID),  {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TT_C_HK_TLM_MID),      {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TT_C_DEVICE_TLM_MID),  {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_GNSS_HK_TLM_MID),      {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_GNSS_DEVICE_TLM_MID),  {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(MGR_HK_TLM_MID),               {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(SYN_HK_TLM_MID),               {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(SCH_HK_TLM_MID),               {0,0},  32},
        {CFE_SB_MSGID_WRAP_VALUE(SCH_DIAG_TLM_MID),             {0,0},  32},

         TILFØJ DISSE TO LINJER: 
        {CFE_SB_MSGID_WRAP_VALUE(HK_HK_TLM_MID), {0,0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HS_HK_TLM_MID), {0,0}, 4},

    }
};
*/

TO_LAB_Subs_t TO_LAB_Subs =
{
    .Subs =
    {
        /* ========================================================= */
        /* TO & CI LAB INFRASTRUCTURE                                */
        /* ========================================================= */
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_LAB_DATA_TYPES_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CI_LAB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CI_LAB_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CI_LAB_SEND_HK_MID), {0, 0}, 4},

        /* ========================================================= */
        /* CFE CORE SERVICES (ES, EVS, SB, TBL, TIME)                */
        /* ========================================================= */
        /* Event Services (EVS) */
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_LONG_EVENT_MSG_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_SHORT_EVENT_MSG_MID), {0, 0}, 32},
        /* Executive Services (ES) */
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_APP_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_MEMSTATS_TLM_MID), {0, 0}, 4},
        /* Software Bus (SB) */
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_SB_STATS_TLM_MID), {0, 0}, 4},
        /* Table Services (TBL) */
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TBL_REG_TLM_MID), {0, 0}, 4},
        /* Time Services (TIME) */
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_TONE_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_1HZ_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CFE_TIME_DIAG_TLM_MID), {0, 0}, 4},

        /* ========================================================= */
        /* CFS HERITAGE APPS (SCH, TO, CI, DS, FM, CF, LC, SC, osv.) */
        /* ========================================================= */
        /* Scheduler (SCH) */
        {CFE_SB_MSGID_WRAP_VALUE(SCH_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SCH_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SCH_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SCH_DIAG_TLM_MID), {0, 0}, 32},
        /* Telemetry Output (TO) */
        {CFE_SB_MSGID_WRAP_VALUE(TO_APP_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_WAKEUP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(TO_OUT_DATA_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(TO_DATA_TYPE_MID), {0, 0}, 4},
        /* Command Ingest (CI) */
        {CFE_SB_MSGID_WRAP_VALUE(CI_APP_CMD_MID), {0, 0}, 4},
        /* Data Storage (DS) */
        {CFE_SB_MSGID_WRAP_VALUE(DS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(DS_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(DS_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(DS_DIAG_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(DS_COMP_TLM_MID), {0, 0}, 4},
        /* File Manager (FM) */
        {CFE_SB_MSGID_WRAP_VALUE(FM_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_FILE_INFO_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_DIR_LIST_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(FM_OPEN_FILES_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(FM_FREE_SPACE_TLM_MID), {0, 0}, 4},
        /* CFDP (CF) */
        {CFE_SB_MSGID_WRAP_VALUE(CF_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_WAKE_UP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_EOT_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CF_CONFIG_TLM_MID), {0, 0}, 4}, 
        /* Limit Checker (LC) */
        {CFE_SB_MSGID_WRAP_VALUE(LC_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(LC_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(LC_SAMPLE_AP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(LC_HK_TLM_MID), {0, 0}, 4},
        /* Stored Command (SC) */
        {CFE_SB_MSGID_WRAP_VALUE(SC_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SC_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SC_1HZ_WAKEUP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SC_HK_TLM_MID), {0, 0}, 4},
        /* Health and Safety (HS) */
        {CFE_SB_MSGID_WRAP_VALUE(HS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HS_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HS_WAKEUP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HS_HK_TLM_MID), {0, 0}, 4},
        /* Housekeeping (HK) */
        {CFE_SB_MSGID_WRAP_VALUE(HK_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_SEND_COMBINED_PKT_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_COMBINED_PKT1_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_COMBINED_PKT2_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_COMBINED_PKT3_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(HK_COMBINED_PKT4_MID), {0, 0}, 4},

        // FØLGENDE ER UDKOMMENTERET FOR AT SIKRE KOMPILERING 
        //   (De mangler #include filer i toppen. Slå dem til senere hvis de skal bruges)
        {CFE_SB_MSGID_WRAP_VALUE(CS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CS_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CS_BACKGROUND_CYCLE_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CS_HK_TLM_MID), {0, 0}, 4},
        
        {CFE_SB_MSGID_WRAP_VALUE(MD_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MD_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MD_WAKEUP_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MD_HK_TLM_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MD_DWELL_PKT_MID_BASE), {0, 0}, 32},
        
        {CFE_SB_MSGID_WRAP_VALUE(MM_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MM_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(MM_HK_TLM_MID), {0, 0}, 4},
        
        {CFE_SB_MSGID_WRAP_VALUE(SBN_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SBN_TLM_MID), {0, 0}, 4},
        
        {CFE_SB_MSGID_WRAP_VALUE(BEACON_APP_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(BEACON_APP_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(BEACON_APP_HK_TLM_MID), {0, 0}, 4},
        

        /* ========================================================= */
        /* RESEARCH / MISSION APPS                                   */
        /* ========================================================= */
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(SAMPLE_DEVICE_TLM_MID), {0, 0}, 32},

        /* ========================================================= */
        /* COMPONENT SPECIFICS (ADCS, EPS, CAM, RADIO, etc.)         */
        /* ========================================================= */
        /* ADCS */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_ADAC_UPDATE_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_DI_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_AD_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_GNC_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_AC_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_ADCS_DO_MID), {0, 0}, 32},
        /* EPS */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_EPS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_EPS_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_EPS_HK_TLM_MID), {0, 0}, 32},
        /* Radio */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RADIO_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RADIO_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RADIO_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RADIO_DEVICE_TLM_MID), {0, 0}, 32},
        /* GPS / Novatel */
        {CFE_SB_MSGID_WRAP_VALUE(NOVATEL_OEM615_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(NOVATEL_OEM615_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(NOVATEL_OEM615_HK_TLM_MID), {0, 0}, 32},
        /* Reaction Wheel */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RW_APP_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RW_APP_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_RW_APP_HK_TLM_MID), {0, 0}, 32},
        /* Coarse Sun Sensor (CSS) */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_CSS_DEVICE_TLM_MID), {0, 0}, 32},
        /* Fine Sun Sensor (FSS) */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_FSS_DEVICE_TLM_MID), {0, 0}, 32},
        /* IMU */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_IMU_DEVICE_TLM_MID), {0, 0}, 32},
        /* Magnetometer (MAG) */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_MAG_DEVICE_TLM_MID), {0, 0}, 32},
        /* Star Tracker */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_STAR_TRACKER_DEVICE_TLM_MID), {0, 0}, 32},
        /* Torquer */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TORQUER_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TORQUER_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TORQUER_HK_TLM_MID), {0, 0}, 32},
        /* Thruster */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_THRUSTER_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_THRUSTER_REQ_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_THRUSTER_HK_TLM_MID), {0, 0}, 32},
        /* Camera (CAM) */
        {CFE_SB_MSGID_WRAP_VALUE(CAM_CMD_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CAM_SEND_HK_MID), {0, 0}, 4},
        {CFE_SB_MSGID_WRAP_VALUE(CAM_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(CAM_EXP_TLM_MID), {0, 0}, 32},

        /* Mgr / SYN */
        {CFE_SB_MSGID_WRAP_VALUE(MGR_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(SYN_HK_TLM_MID), {0, 0}, 32},
        /* GNSS */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_GNSS_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_GNSS_DEVICE_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_GNSS_SAT_HELLO_TLM_MID), {0, 0}, 32},
        /* TT&C */
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TT_C_HK_TLM_MID), {0, 0}, 32},
        {CFE_SB_MSGID_WRAP_VALUE(GENERIC_TT_C_DEVICE_TLM_MID), {0, 0}, 32}
    }
};

CFE_TBL_FILEDEF(TO_LAB_Subs, TO_LAB_APP.TO_LAB_Subs, TO Lab Sub Tbl, to_lab_sub.tbl)
