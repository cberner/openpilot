/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup CameraStab Camera Stabilization Module
 * @brief Camera stabilization module
 * Updates accessory outputs with values appropriate for camera stabilization
 * @{
 *
 * @file       camerastab.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Stabilize camera against the roll pitch and yaw of aircraft
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * Output object: Accessory
 *
 * This module will periodically calculate the output values for stabilizing the camera
 *
 * UAVObjects are automatically generated by the UAVObjectGenerator from
 * the object definition XML file.
 *
 * Modules have no API, all communication to other modules is done through UAVObjects.
 * However modules may use the API exposed by shared libraries.
 * See the OpenPilot wiki for more details.
 * http://www.openpilot.org/OpenPilot_Application_Architecture
 *
 */

#include "openpilot.h"

#include "accessorydesired.h"
#include "attitudeactual.h"
#include "camerastabsettings.h"
#include "cameradesired.h"
#include "hwsettings.h"

//
// Configuration
//
#define SAMPLE_PERIOD_MS		10

// Private types

// Private variables
static struct CameraStab_data {
	portTickType lastSysTime;
	float inputs[CAMERASTABSETTINGS_INPUT_NUMELEM];
	float inputs_filtered[CAMERASTABSETTINGS_INPUT_NUMELEM];
} *csd;

// Private functions
static void attitudeUpdated(UAVObjEvent* ev);
static float bound(float val, float limit);

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t CameraStabInitialize(void)
{
	bool cameraStabEnabled;

#ifdef MODULE_CameraStab_BUILTIN
	cameraStabEnabled = true;
#else
	uint8_t optionalModules[HWSETTINGS_OPTIONALMODULES_NUMELEM];

	HwSettingsInitialize();
	HwSettingsOptionalModulesGet(optionalModules);

	if (optionalModules[HWSETTINGS_OPTIONALMODULES_CAMERASTAB] == HWSETTINGS_OPTIONALMODULES_ENABLED)
		cameraStabEnabled = true;
	else
		cameraStabEnabled = false;
#endif

	if (cameraStabEnabled) {

		// allocate and initialize the static data storage only if module is enabled
		csd = (struct CameraStab_data *) pvPortMalloc(sizeof(struct CameraStab_data));
		if (!csd)
			return -1;

		// make sure that all inputs[] and inputs_filtered[] are zeroed
		memset(csd, 0, sizeof(struct CameraStab_data));
		csd->lastSysTime = xTaskGetTickCount();

		AttitudeActualInitialize();
		CameraStabSettingsInitialize();
		CameraDesiredInitialize();

		UAVObjEvent ev = {
			.obj = AttitudeActualHandle(),
			.instId = 0,
			.event = 0,
		};
		EventPeriodicCallbackCreate(&ev, attitudeUpdated, SAMPLE_PERIOD_MS / portTICK_RATE_MS);

		return 0;
	}

	return -1;
}

/* stub: module has no module thread */
int32_t CameraStabStart(void)
{
	return 0;
}

MODULE_INITCALL(CameraStabInitialize, CameraStabStart)

static void attitudeUpdated(UAVObjEvent* ev)
{
	if (ev->obj != AttitudeActualHandle())
		return;

	AccessoryDesiredData accessory;

	CameraStabSettingsData cameraStab;
	CameraStabSettingsGet(&cameraStab);

	// Check how long since last update, time delta between calls in ms
	portTickType thisSysTime = xTaskGetTickCount();
	float dT = (thisSysTime > csd->lastSysTime) ?
			(thisSysTime - csd->lastSysTime) / portTICK_RATE_MS :
			(float)SAMPLE_PERIOD_MS / 1000.0f;
	csd->lastSysTime = thisSysTime;

	// Read any input channels and apply LPF
	for (uint8_t i = 0; i < CAMERASTABSETTINGS_INPUT_NUMELEM; i++) {
		if (cameraStab.Input[i] != CAMERASTABSETTINGS_INPUT_NONE) {
			if (AccessoryDesiredInstGet(cameraStab.Input[i] - CAMERASTABSETTINGS_INPUT_ACCESSORY0, &accessory) == 0) {
				float input_rate;
				switch (cameraStab.StabilizationMode[i]) {
				case CAMERASTABSETTINGS_STABILIZATIONMODE_ATTITUDE:
					csd->inputs[i] = accessory.AccessoryVal * cameraStab.InputRange[i];
					break;
				case CAMERASTABSETTINGS_STABILIZATIONMODE_AXISLOCK:
					input_rate = accessory.AccessoryVal * cameraStab.InputRate[i];
					if (fabs(input_rate) > cameraStab.MaxAxisLockRate)
						csd->inputs[i] = bound(csd->inputs[i] + input_rate * dT / 1000.0f, cameraStab.InputRange[i]);
					break;
				default:
					PIOS_Assert(0);
				}

				// bypass LPF calculation if ResponseTime is zero
				float rt = (float)cameraStab.ResponseTime[i];
				if (rt)
					csd->inputs_filtered[i] = (rt / (rt + dT)) * csd->inputs_filtered[i]
								+ (dT / (rt + dT)) * csd->inputs[i];
				else
					csd->inputs_filtered[i] = csd->inputs[i];
			}
		}
	}

	// Set output channels
	float attitude;
	float output;

	AttitudeActualRollGet(&attitude);
	output = bound((attitude + csd->inputs_filtered[CAMERASTABSETTINGS_INPUT_ROLL]) / cameraStab.OutputRange[CAMERASTABSETTINGS_OUTPUTRANGE_ROLL], 1.0f);
	CameraDesiredRollSet(&output);

	AttitudeActualPitchGet(&attitude);
	output = bound((attitude + csd->inputs_filtered[CAMERASTABSETTINGS_INPUT_PITCH]) / cameraStab.OutputRange[CAMERASTABSETTINGS_OUTPUTRANGE_PITCH], 1.0f);
	CameraDesiredPitchSet(&output);

	AttitudeActualYawGet(&attitude);
	output = bound((attitude + csd->inputs_filtered[CAMERASTABSETTINGS_INPUT_YAW]) / cameraStab.OutputRange[CAMERASTABSETTINGS_OUTPUTRANGE_YAW], 1.0f);
	CameraDesiredYawSet(&output);
}

float bound(float val, float limit)
{
	return (val > limit) ? limit :
		(val < -limit) ? -limit :
		val;
}
/**
  * @}
  */

/**
 * @}
 */
