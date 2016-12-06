/*******************************************************************************
** This file is provided under a dual BSD/GPLv2 license.  When using or
** redistributing this file, you may do so under either license.
**
** GPL LICENSE SUMMARY
**
** Copyright (c) 2013-2016 Intel Corporation All Rights Reserved
**
** This program is free software; you can redistribute it and/or modify it under
** the terms of version 2 of the GNU General Public License as published by the
** Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
** details.
**
** You should have received a copy of the GNU General Public License along with
** this program; if not, write to the Free Software  Foundation, Inc.,
** 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
** The full GNU General Public License is included in this distribution in the
** file called LICENSE.GPL.
**
** BSD LICENSE
**
** Copyright (c) 2013-2016 Intel Corporation All Rights Reserved
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**
** * Redistributions of source code must retain the above copyright notice, this
**   list of conditions and the following disclaimer.
** * Redistributions in binary form must reproduce the above copyright notice,
**   this list of conditions and the following disclaimer in the documentation
**   and/or other materials provided with the distribution.
** * Neither the name of Intel Corporation nor the names of its contributors may
**   be used to endorse or promote products derived from this software without
**   specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,  SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
*******************************************************************************/

#pragma once

#include <string.h>
#include "esif_sdk.h"
#include "esif_sdk_event_type.h"
#include "esif_sdk_event_guid.h"
#include "esif_ccb_memory.h"

/*
* Event GUID/Type Mapping
*/

/* Use these Helper Macros for Converting to/from GUID or Event Type */
#define esif_event_map_guid2type(guid, type)	esif_event_map(guid, type, ESIF_TRUE)
#define esif_event_map_type2guid(guid, type)	esif_event_map(guid, type, ESIF_FALSE)

/* Convert Event GUID to/from Event Type */
static ESIF_INLINE Bool esif_event_map(
	esif_guid_t *guidPtr,
	esif_event_type_t *typePtr,
	Bool isFromGuid
	)
{
	struct {
		esif_guid_t		guid;
		esif_event_type_t 	type;
	} esif_event_map_table[] = {
		{ ACTIVE_RELATIONSHIP_CHANGED,
		  ESIF_EVENT_APP_ACTIVE_RELATIONSHIP_CHANGED },
		{ CONNECTED_STANDBY_ENTRY,
		  ESIF_EVENT_APP_CONNECTED_STANDBY_ENTRY },
		{ CONNECTED_STANDBY_EXIT,
		  ESIF_EVENT_APP_CONNECTED_STANDBY_EXIT },
		{ FOREGROUND_CHANGED,
		  ESIF_EVENT_APP_FOREGROUND_CHANGED },
		{ THERMAL_RELATIONSHIP_CHANGED,
		  ESIF_EVENT_APP_THERMAL_RELATIONSHIP_CHANGED },
		{ CORE_CAPABILITY_CHANGED,
		  ESIF_EVENT_DOMAIN_CORE_CAPABILITY_CHANGED },
		{ CTDP_CAPABILITY_CHANGED,
		  ESIF_EVENT_DOMAIN_CTDP_CAPABILITY_CHANGED },
		{ DISPLAY_CAPABILITY_CHANGED,
		  ESIF_EVENT_DOMAIN_DISPLAY_CAPABILITY_CHANGED },
		{ DISPLAY_STATUS_CHANGED,
		  ESIF_EVENT_DOMAIN_DISPLAY_STATUS_CHANGED },
		{ PERF_CAPABILITY_CHANGED,
		  ESIF_EVENT_DOMAIN_PERF_CAPABILITY_CHANGED },
		{ PERF_CONTROL_CHANGED,
		  ESIF_EVENT_DOMAIN_PERF_CONTROL_CHANGED },
		{ POWER_CAPABILITY_CHANGED,
		  ESIF_EVENT_DOMAIN_POWER_CAPABILITY_CHANGED },
		{ POWER_THRESHOLD_CROSSED,
		  ESIF_EVENT_DOMAIN_POWER_THRESHOLD_CROSSED },
		{ PRIORITY_CHANGED,
		  ESIF_EVENT_DOMAIN_PRIORITY_CHANGED },
		{ TEMP_THRESHOLD_CROSSED,
		  ESIF_EVENT_DOMAIN_TEMP_THRESHOLD_CROSSED },
		{ CREATE,
		  ESIF_EVENT_PARTICIPANT_CREATE },
		{ RESUME,
		  ESIF_EVENT_PARTICIPANT_RESUME },
		{ SHUTDOWN,
		  ESIF_EVENT_PARTICIPANT_SHUTDOWN },
		{ SPEC_INFO_CHANGED,
		  ESIF_EVENT_PARTICIPANT_SPEC_INFO_CHANGED },
		{ SUSPEND,
		  ESIF_EVENT_PARTICIPANT_SUSPEND },
		{ UNREGISTER,
		  ESIF_EVENT_PARTICIPANT_UNREGISTER },
		{ ACPI,
		  ESIF_EVENT_ACPI },
		{ ACTION_LOADED,
		  ESIF_EVENT_ACTION_LOADED },
		{ ACTION_UNLOADED,
		  ESIF_EVENT_ACTION_UNLOADED },
		{ ACTIVE_CONTROL_POINT_RELATIONSHIP_TABLE_CHANGED,
		  ESIF_EVENT_ACTIVE_CONTROL_POINT_RELATIONSHIP_TABLE_CHANGED },
		{ ADAPTIVE_PERFORMANCE_ACTIONS_TABLE_CHANGED,
		  ESIF_EVENT_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE_CHANGED },
		{ ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE_CHANGED,
		  ESIF_EVENT_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE_CHANGED },
		{ ADAPTIVE_PERFORMANCE_PARTICIPANT_CONDITION_TABLE_CHANGED,
		  ESIF_EVENT_ADAPTIVE_PERFORMANCE_PARTICIPANT_CONDITION_TABLE_CHANGED },
		{ APP_LOADED,
		  ESIF_EVENT_APP_LOADED },
		{ APP_UNLOADED,
		  ESIF_EVENT_APP_UNLOADED },
		{ APP_UNLOADING,
		  ESIF_EVENT_APP_UNLOADING },
		{ BATTERY_INFORMATION_CHANGED,
		  ESIF_EVENT_BATTERY_INFORMATION_CHANGED },
		{ BATTERY_STATUS_CHANGED,
		  ESIF_EVENT_BATTERY_STATUS_CHANGED },
		{ CHARGER_TYPE_CHANGED,
		  ESIF_EVENT_CHARGER_TYPE_CHANGED },
		{ DEVICE_ORIENTATION_CHANGED,
		  ESIF_EVENT_DEVICE_ORIENTATION_CHANGED },
		{ DISPLAY_ORIENTATION_CHANGED,
		  ESIF_EVENT_DISPLAY_ORIENTATION_CHANGED },
		{ DPTF_PARTICIPANT_ACTIVITY_LOGGING_DISABLED,
		  ESIF_EVENT_DPTF_PARTICIPANT_ACTIVITY_LOGGING_DISABLED },
		{ DPTF_PARTICIPANT_ACTIVITY_LOGGING_ENABLED,
		  ESIF_EVENT_DPTF_PARTICIPANT_ACTIVITY_LOGGING_ENABLED },
		{ DPTF_PARTICIPANT_CONTROL_ACTION,
		  ESIF_EVENT_DPTF_PARTICIPANT_CONTROL_ACTION },
		{ DPTF_POLICY_ACTIVITY_LOGGING_DISABLED,
		  ESIF_EVENT_DPTF_POLICY_ACTIVITY_LOGGING_DISABLED },
		{ DPTF_POLICY_ACTIVITY_LOGGING_ENABLED,
		  ESIF_EVENT_DPTF_POLICY_ACTIVITY_LOGGING_ENABLED },
		{ DPTF_POLICY_LOADED_UNLOADED,
		  ESIF_EVENT_DPTF_POLICY_LOADED_UNLOADED },
		{ EMERGENCY_CALL_MODE_TABLE_CHANGED,
		  ESIF_EVENT_EMERGENCY_CALL_MODE_TABLE_CHANGED },
		{ LOG_VERBOSITY_CHANGED,
		  ESIF_EVENT_LOG_VERBOSITY_CHANGED },
		{ MAX_BATTERY_POWER_CHANGED,
		  ESIF_EVENT_MAX_BATTERY_POWER_CHANGED },
		{ MOTION_CHANGED,
		  ESIF_EVENT_MOTION_CHANGED },
		{ OEM_VARS_CHANGED,
		  ESIF_EVENT_OEM_VARS_CHANGED },
		{ OS_BATTERY_PERCENT_CHANGED,
		  ESIF_EVENT_OS_BATTERY_PERCENT_CHANGED },
		{ OS_CTDP_CAPABILITY_CHANGED,
		  ESIF_EVENT_OS_CTDP_CAPABILITY_CHANGED },
		{ OS_DOCK_MODE_CHANGED,
		  ESIF_EVENT_OS_DOCK_MODE_CHANGED },
		{ OS_LID_STATE_CHANGED,
		  ESIF_EVENT_OS_LID_STATE_CHANGED },
		{ OS_MOBILE_NOTIFICATION,
		  ESIF_EVENT_OS_MOBILE_NOTIFICATION },
		{ OS_PLATFORM_TYPE_CHANGED,
		  ESIF_EVENT_OS_PLATFORM_TYPE_CHANGED },
		{ OS_POWER_SOURCE_CHANGED,
		  ESIF_EVENT_OS_POWER_SOURCE_CHANGED },
		{ OS_POWERSCHEME_PERSONALITY_CHANGED,
		  ESIF_EVENT_OS_POWERSCHEME_PERSONALITY_CHANGED },
		{ PARTICIPANT_UNREGISTER_COMPLETE,
		  ESIF_EVENT_PARTICIPANT_UNREGISTER_COMPLETE },
		{ PASSIVE_TABLE_CHANGED,
		  ESIF_EVENT_PASSIVE_TABLE_CHANGED },
		{ PID_ALGORITHM_TABLE_CHANGED,
		  ESIF_EVENT_PID_ALGORITHM_TABLE_CHANGED },
		{ PLATFORM_BATTERY_STEADY_STATE_CHANGED,
		  ESIF_EVENT_PLATFORM_BATTERY_STEADY_STATE_CHANGED },
		{ PLATFORM_POWER_SOURCE_CHANGED,
		  ESIF_EVENT_PLATFORM_POWER_SOURCE_CHANGED },
		{ PLATFORM_REST_OF_POWER_CHANGED,
		  ESIF_EVENT_PLATFORM_REST_OF_POWER_CHANGED },
		{ POWER_BOSS_ACTIONS_TABLE_CHANGED,
		  ESIF_EVENT_POWER_BOSS_ACTIONS_TABLE_CHANGED },
		{ POWER_BOSS_CONDITIONS_TABLE_CHANGED,
		  ESIF_EVENT_POWER_BOSS_CONDITIONS_TABLE_CHANGED },
		{ POWER_BOSS_MATH_TABLE_CHANGED,
		  ESIF_EVENT_POWER_BOSS_MATH_TABLE_CHANGED },
		{ POWER_SOURCE_CHANGED,
		  ESIF_EVENT_POWER_SOURCE_CHANGED },
		{ RF_CONNECTION_STATUS_CHANGED,
		  ESIF_EVENT_RF_CONNECTION_STATUS_CHANGED },
		{ RF_PROFILE_CHANGED,
		  ESIF_EVENT_RF_PROFILE_CHANGED },
		{ SYSTEM_COOLING_POLICY_CHANGED,
		  ESIF_EVENT_SYSTEM_COOLING_POLICY_CHANGED },
		{ VIRTUAL_SENSOR_CALIB_TABLE_CHANGED,
		  ESIF_EVENT_VIRTUAL_SENSOR_CALIB_TABLE_CHANGED },
		{ VIRTUAL_SENSOR_POLLING_TABLE_CHANGED,
		  ESIF_EVENT_VIRTUAL_SENSOR_POLLING_TABLE_CHANGED },
		{ VIRTUAL_SENSOR_RECALC_CHANGED,
		  ESIF_EVENT_VIRTUAL_SENSOR_RECALC_CHANGED },
		{ 0 }
	};
	esif_guid_t null_guid = {0};
	int j;

	if (guidPtr && typePtr) {
		for (j = 0; memcmp(esif_event_map_table[j].guid, null_guid, sizeof(null_guid)) != 0; j++) {
			if (isFromGuid && memcmp(esif_event_map_table[j].guid, *guidPtr, sizeof(*guidPtr)) == 0) {
				*typePtr = esif_event_map_table[j].type;
				return ESIF_TRUE;
			}
			else if (!isFromGuid && esif_event_map_table[j].type == *typePtr) {
				esif_ccb_memcpy(*guidPtr, esif_event_map_table[j].guid, sizeof(*guidPtr));
				return ESIF_TRUE;
			}
		}
	}
	return ESIF_FALSE;
}
