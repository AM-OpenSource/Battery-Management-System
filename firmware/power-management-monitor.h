/* STM32F1 Power Management for Solar Power

This header file contains defines and prototypes specific to the monitoring
task.

Initial 29 September 2013
*/

/*
 * This file is part of the battery-management-system project.
 *
 * Copyright 2013 K. Sarkies <ksarkies@internode.on.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef POWER_MANAGEMENT_MONITOR_H_
#define POWER_MANAGEMENT_MONITOR_H_

#include <stdint.h>
#include <stdbool.h>

#include "power-management-objdic.h"

/* Delay to allow time for first measurements to come in */
#define MONITOR_STARTUP_DELAY       (( portTickType )1000/portTICK_RATE_MS )

/*--------------------------------------------------------------------------*/
/* Calibration Constants */
/* Threshold to test for valid offset measurement (without power on the
interface, the result is maximum negative, around -4000) */
#define CALIBRATION_THRESHOLD       -50
/* Arbitrary high value to start off the minimum value offset computation */
#define OFFSET_START_VALUE          100
/* Number of tests of switch combinations */
#define NUM_TESTS                   NUM_IFS+1

/*--------------------------------------------------------------------------*/
/* Battery capacity scale to precision of SoC tracking from sample time
(500 ms in this case) to hours. */

/*--------------------------------------------------------------------------*/
/* Battery Monitoring Strategy Fields */
#define SEPARATE_LOAD       1 << 0
#define PRESERVE_ISOLATION  1 << 1

/*--------------------------------------------------------------------------*/
/* Prototypes */
/*--------------------------------------------------------------------------*/
void prvMonitorTask(void *pvParameters);
int16_t getBatteryCurrentOffset(int battery);
int16_t getLoadCurrentOffset(int load);
int16_t getPanelCurrentOffset(int panel);
battery_Hl_States getBatteryHealthState(int i);
int16_t getBatterySoC(int battery);
int16_t getBatteryUnderLoad(void);
void setBatteryUnderLoad(int battery);
void setBatterySoC(int battery,int16_t soc);
void resetBatterySoC(int battery);
void startCalibration();
void setBatteryMissing(int battery, bool missing);
int16_t computeSoC(uint32_t voltage, uint32_t temperature, battery_Type type);
void checkMonitorWatchdog(void);
void startMonitorTask(void);

#endif

