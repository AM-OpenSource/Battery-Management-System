/** @defgroup Objdic_file Object Dictionary

@brief Definition of CANopen Object Dictionary Variables

These are made available to an
external PC and to other processing modules which may be CANopen devices or
tasks running on the same microcontroller.

Note these definitions cannot be placed in the header file.
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

/**@{*/

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "power-management-objdic.h"
#include "power-management-hardware.h"

/* Byte pattern that indicates if a valid NVM config data block is present */
#define VALID_BLOCK                 0xD5

/*--------------------------------------------------------------------------*/
/* Preset the config data block in FLASH to a given pattern to indicate unused. */
union ConfigGroup configDataBlock __attribute__ ((section (".configBlock"))) = {{0xA5}};
union ConfigGroup configData;

/*--------------------------------------------------------------------------*/
/** @brief Initialise Global Configuration Variables

This determines if configuration variables are present in NVM, and if so
reads them in. The first entry is checked against a preprogrammed value to
determine if the block is a valid configuration block. This allows the program
to determine whether to use the block stored in FLASH or to use defaults.
*/

void setGlobalDefaults(void)
{
    flashReadData((uint32_t*)configDataBlock.data,
                   configData.data,sizeof(configData.config));
    if (configData.config.validBlock == VALID_BLOCK) return;
/* Set default communications control variables */
    configData.config.measurementSend = true;
    configData.config.debugMessageSend = false;
    configData.config.enableSend = false;
/* Set default recording control variables */
    configData.config.recording = false;
/* Set default battery parameters */
    configData.config.batteryCapacity[0] = BATTERY_CAPACITY_1;
    configData.config.batteryCapacity[1] = BATTERY_CAPACITY_2;
    configData.config.batteryCapacity[2] = BATTERY_CAPACITY_3;
    configData.config.batteryType[0] = BATTERY_TYPE_1;
    configData.config.batteryType[1] = BATTERY_TYPE_2;
    configData.config.batteryType[2] = BATTERY_TYPE_3;
    configData.config.alphaR = 100;             /* about 0.4 */
    configData.config.alphaV = 256;             /* No Filter */
    configData.config.alphaC = 180;     /* about 0.7, for detecting float state. */
    uint8_t i=0;
    for (i=0; i<NUM_BATS; i++) setBatteryChargeParameters(i);
    for (i=0; i<NUM_IFS; i++) setCurrentOffset(i,0);    /* Zero current offsets. */
/* Set default tracking parameters */
    configData.config.autoTrack = false;    /* Don't track unless instructed */
    configData.config.monitorStrategy = 0xFF;   /* All strategies on */
    configData.config.panelSwitchSetting = 0;
    configData.config.lowVoltage = LOW_VOLTAGE;
    configData.config.criticalVoltage = CRITICAL_VOLTAGE;
    configData.config.lowSoC = LOW_SOC;
    configData.config.criticalSoC = CRITICAL_SOC;
    configData.config.floatBulkSoC = FLOAT_BULK_SOC;
/* Set default charging parameters */
    configData.config.chargerStrategy = 0;  /* All strategies off */
    configData.config.restTime = REST_TIME;
    configData.config.absorptionTime = ABSORPTION_TIME;
    configData.config.minDutyCycle = MIN_DUTYCYCLE ;
    configData.config.floatTime = FLOAT_DELAY;
/* Set default system control variables */
    configData.config.watchdogDelay = WATCHDOG_DELAY;
    configData.config.chargerDelay = CHARGER_DELAY;
    configData.config.measurementDelay = MEASUREMENT_DELAY;
    configData.config.monitorDelay = MONITOR_DELAY;
    configData.config.calibrationDelay = CALIBRATION_DELAY;
}

/*--------------------------------------------------------------------------*/
/** @brief Write Configuration Data Block to Flash

Refer to the linker script for allocation of the config data block on a
page boundary. If this is not done, a page erase may destroy valid data or code.

The current datablock is written to flash with the first entry set to a value
that indicates whether the block is a valid programmed configuration block.

@returns uint32_t result code. 0 success, 1 fail.
*/

uint32_t writeConfigBlock(void)
{
    configData.config.validBlock = VALID_BLOCK;
    return flashWriteData((uint32_t*)configDataBlock.data,
                          configData.data, sizeof(configData.config));
}

/*--------------------------------------------------------------------------*/
/** @brief Set the Battery Charge Parameters given the Type

The voltage parameters are set for recommended values at 25C.

@param[in] battery: int 0..NUM_BATS-1
*/

void setBatteryChargeParameters(int battery)
{
    if (configData.config.batteryType[battery] == wetT)
    {
        configData.config.absorptionVoltage[battery] = 3686;        /* 14.4V */
        configData.config.floatVoltage[battery] = 3379;             /* 13.2V */
    }
    else if (configData.config.batteryType[battery] == agmT)
    {
        configData.config.absorptionVoltage[battery] = 3738;        /* 14.6V */
        configData.config.floatVoltage[battery] = 3482;             /* 13.6V */
    }
    else if (configData.config.batteryType[battery] == gelT)
    {
        configData.config.absorptionVoltage[battery] = 3584;        /* 14.0V */
        configData.config.floatVoltage[battery] = 3532;             /* 13.8V */
    }
    configData.config.floatStageCurrentScale[battery] = 50;
    configData.config.bulkCurrentLimitScale[battery] = 5;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Battery Type

@param[in] battery: int 0..NUM_BATS-1
@returns battery_Type Battery Type
*/

battery_Type getBatteryType(int battery)
{
    return configData.config.batteryType[battery];
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Battery Capacity

@param[in] battery: int 0..NUM_BATS-1
@returns int16_t Battery Capacity
*/

int16_t getBatteryCapacity(int battery)
{
    return configData.config.batteryCapacity[battery];
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Battery Bulk Current Limit

@param[in] battery: int 0..NUM_BATS-1
@returns int16_t Battery Bulk Current Limit
*/

int16_t getBulkCurrentLimit(int battery)
{
    return configData.config.batteryCapacity[battery]*256/
            configData.config.bulkCurrentLimitScale[battery];
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Battery Float Current Cutoff

@param[in] battery: int 0..NUM_BATS-1
@returns int16_t Battery Float Current Cutoff
*/

int16_t getFloatStageCurrent(int battery)
{
    return configData.config.batteryCapacity[battery]*256/
            configData.config.floatStageCurrentScale[battery];
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Absorption Phase Voltage Limit

@param[in] battery: int 0..NUM_BATS-1
@returns int16_t Absorption Phase Voltage Limit
*/

int16_t getAbsorptionVoltage(int battery)
{
    return configData.config.absorptionVoltage[battery];
}
/*--------------------------------------------------------------------------*/
/** @brief Get the Float Phase Voltage Limit

@param[in] battery: int 0..NUM_BATS-1
@returns int16_t Float Phase Voltage Limit
*/

int16_t getFloatVoltage(int battery)
{
    return configData.config.floatVoltage[battery];
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Forgetting factor for Charger Voltage Smoothing

@returns int16_t Forgetting factor
*/

int16_t getAlphaV(void)
{
    return configData.config.alphaV;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Forgetting factor for Charger Current Smoothing

@returns int16_t Forgetting factor
*/

int16_t getAlphaC(void)
{
    return configData.config.alphaC;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Forgetting factor for Resistance Estimator Smoothing

@returns int16_t Forgetting factor
*/

int16_t getAlphaR(void)
{
    return configData.config.alphaR;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Current Offset for A/D measurements

@param interface: uint8_t the interface 0...NUM_IFS.
@returns int16_t Current Offset
*/

int16_t getCurrentOffset(uint8_t interface)
{
    return configData.config.currentOffsets.data[interface];
}

/*--------------------------------------------------------------------------*/
/** @brief Set the Current Offset for A/D measurements

@param interface: uint8_t the interface 0...NUM_IFS.
@param offset: uint16_t current offset value
*/

void setCurrentOffset(uint8_t interface, int16_t offset)
{
    configData.config.currentOffsets.data[interface] = offset;
}

/*--------------------------------------------------------------------------*/
/** @brief Provide the Watchdog Task Time Interval

This is the time between decision updates.

@returns portTickType Watchdog Task Time Interval
*/

portTickType getWatchdogDelay(void)
{
    return configData.config.watchdogDelay;
}

/*--------------------------------------------------------------------------*/
/** @brief Provide the Charging Task Time Interval

This is the time between decision updates.

@returns portTickType Charging Task Time Interval
*/

portTickType getChargerDelay(void)
{
    return configData.config.chargerDelay;
}

/*--------------------------------------------------------------------------*/
/** @brief Provide the Measurement Task Time Interval

This is the time between measurement updates.

@returns portTickType Measurement Task Time Interval
*/

portTickType getMeasurementDelay(void)
{
    return configData.config.measurementDelay;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Monitor Task Time Interval

This is the time between decision updates.

@returns portTickType Monitor Task Time Interval
*/

portTickType getMonitorDelay(void)
{
    return configData.config.monitorDelay;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Calibration Time Interval

This is the time between measurement updates.

@returns portTickType Calibration Time Interval
*/

portTickType getCalibrationDelay(void)
{
    return configData.config.calibrationDelay;
}

/*--------------------------------------------------------------------------*/
/** @brief Get any Manual Switch Setting

@returns uint8_t switch setting
*/

uint8_t getPanelSwitchSetting(void)
{
    return configData.config.panelSwitchSetting;
}

/*--------------------------------------------------------------------------*/
/** @brief Provide any Manual Switch Setting

@param setting: uint8_t switch setting
*/

void setPanelSwitchSetting(uint8_t setting)
{
    configData.config.panelSwitchSetting = setting;
}

/*--------------------------------------------------------------------------*/
/** @brief Get Recording switch

True if recording has been requested, false otherwise.

@returns bool recording setting.
*/

bool isRecording(void)
{
    return configData.config.recording;
}

/*--------------------------------------------------------------------------*/
/** @brief Get Automatic Tracking switch

True if automatic tracking has been requested, false otherwise.

@returns bool Autotrack setting.
*/

bool isAutoTrack(void)
{
    return configData.config.autoTrack;
}

/*--------------------------------------------------------------------------*/
/** @brief Automatic Tracking switch

True if automatic tracking has been requested, false otherwise.

@returns uint8_t Monitor Strategy byte.
*/

uint8_t getMonitorStrategy(void)
{
    return configData.config.monitorStrategy;
}

/*--------------------------------------------------------------------------*/
/** @brief Return a status word showing software controls.

bit  0   if autoTrack,
bit  1   if recording,
bit  3   if measurements are being sent
bit  4   if debug messages are being sent
bits 5,6
bit  7   avoid load on charging battery
bit  8   maintain battery under isolation

@returns uint16_t status of controls
*/
uint16_t getControls(void)
{
    uint16_t controls = 0;
    if (configData.config.autoTrack) controls |= 1<<0;
    if (configData.config.recording) controls |= 1<<1;
    if (configData.config.measurementSend) controls |= 1<<3;
    if (configData.config.debugMessageSend) controls |= 1<<4;
    return controls;
}

/**@}*/

