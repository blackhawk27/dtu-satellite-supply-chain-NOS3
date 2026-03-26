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
 *   Checksum (CS) Application Message IDs
 */
#ifndef DEFAULT_CS_MSGID_VALUES_H
#define DEFAULT_CS_MSGID_VALUES_H

/*
 * Hardcoded MID values for non-EDS (legacy cFE) builds.
 * CFE_PLATFORM_CMD_TOPICID_TO_MIDV / CFE_PLATFORM_TLM_TOPICID_TO_MIDV are
 * Draco-era macros that do not exist in this cFE.  Use a token-paste
 * lookup table instead so that default_cs_msgids.h compiles unchanged.
 */
#define CFE_PLATFORM_CS_CMD_MIDVAL_CMD              0x189F
#define CFE_PLATFORM_CS_CMD_MIDVAL_SEND_HK          0x18A0
#define CFE_PLATFORM_CS_CMD_MIDVAL_BACKGROUND_CYCLE 0x18A1
#define CFE_PLATFORM_CS_TLM_MIDVAL_HK_TLM           0x08A4

#define CFE_PLATFORM_CS_CMD_MIDVAL(x) CFE_PLATFORM_CS_CMD_MIDVAL_##x
#define CFE_PLATFORM_CS_TLM_MIDVAL(x) CFE_PLATFORM_CS_TLM_MIDVAL_##x

#endif
