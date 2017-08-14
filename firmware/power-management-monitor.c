/** @defgroup Monitor_file Monitor

@brief Management of Allocation of Charger and loads

This task accesses the various measured and estimated parameters of the
batteries, loads and panel to make decisions about switch settings and
load disconnect/reconnect. The decisions made here involve the set of batteries
as a whole rather than individual batteries.

The decisions determine how to connect loads and solar panel to the different
batteries in order to ensure continuous service and long battery life. The
batteries are connected to the charger at a low level of SoC, to the loads at a
high level of SoC, and are isolated for a period of time to obtain a reference
measurement of the SoC from the open circuit voltage. Loads are progressively
disconnected as the batteries pass to the low and critically low charge states.

On external command the interface currents and SoC of the batteries will be
calibrated.

On external command the task will automatically track and manage battery to load
and battery charging. Tracking will always occur but switches will not be set
until auto-tracking is enabled.

@note Non-integer variables are scaled by a factor 256 (8 bit shift) to allow
fixed point arithmetic to be performed rapidly using integer values. This avoids
the use of floating point which will be costly for processors lacking a hardware
FPU.

Initial 29 September 2013
Updated 15 November 2016
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
#include <stdlib.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "power-management-board-defs.h"
#include "power-management.h"
#include "power-management-hardware.h"
#include "power-management-objdic.h"
#include "power-management-lib.h"
#include "power-management-time.h"
#include "power-management-file.h"
#include "power-management-comms.h"
#include "power-management-measurement.h"
#include "power-management-charger.h"
#include "power-management-monitor.h"

/*--------------------------------------------------------------------------*/
/* Local Prototypes */
static void initGlobals(void);

/*--------------------------------------------------------------------------*/
/* Global Variables */
/* These configuration variables are part of the Object Dictionary. */
/* This is defined in power-management-objdic and is updated in response to
received messages. */
extern union ConfigGroup configData;

/*--------------------------------------------------------------------------*/
/* Local Persistent Variables */
static uint8_t monitorWatchdogCount;
static bool calibrate;
/* All current, voltage, SoC, charge variables times 256. */
static struct batteryStates battery[NUM_BATS];
static union InterfaceGroup currentOffsets;
static union InterfaceGroup currents;
static union InterfaceGroup voltages;
static uint8_t batteryUnderCharge;
static uint8_t batteryUnderLoad;
static bool chargerOff;                 /* At night the charger is disabled */

/*--------------------------------------------------------------------------*/
/** @brief <b>Monitoring Task</b>

This is a long-running task, monitoring the state of the
batteries, deciding which one to charge and which to place under load, switching
them out at intervals and when critically low, and resetting the state of charge
estimation algorithm during lightly loaded periods.
*/

void prvMonitorTask(void *pvParameters)
{
    pvParameters = pvParameters;
    initGlobals();

    uint16_t decisionStatus = 0;    

/* Short delay to allow measurement task to produce results */
    vTaskDelay(MONITOR_STARTUP_DELAY );

/* Main loop */
    while (true)
    {
/*------------- CALIBRATION -----------------------*/
/**
<b>Calibration:</b> Perform a calibration sequence to zero the currents
(see docs). Also estimate the State of Charge from the Open Circuit Voltages.
For this purpose the system should have been left in a quiescent state for at
least two hours. */

        if (calibrate)
        {
/* Keep aside to restore after calibration. */
            uint8_t switchSettings = getSwitchControlBits();
/* Results for all 7 tests */
            int16_t results[NUM_TESTS][NUM_IFS];
            uint8_t test;
            uint8_t i;

/* Zero the offsets. */
            for (i=0; i<NUM_IFS; i++) currentOffsets.data[i] = 0;

/* Set switches and collect the results */
            for (test=0; test<NUM_TESTS ; test++)
            {
/* First turn off all switches */
                for (i=0; i<NUM_LOADS+NUM_PANELS; i++) setSwitch(0,i);
/* Connect load 2 to each battery in turn. */
                if (test < NUM_BATS) setSwitch(test+1,1);
/* Then connect load 1 to each battery in turn. Last test is all
switches off to allow the panel to be measured. */
                else if (test < NUM_TESTS-1) setSwitch(test-NUM_BATS+1,0);
/* Delay a few seconds to let the measurements settle. Current should settle
quickly but terminal voltage may take some time, which could slightly affect
some currents. */
                vTaskDelay(getCalibrationDelay());
/* Check to see if a battery is missing as a result of setting loads by reading
the LED indicators on the battery interface boards.
@note Missing batteries not under load will not show as missing due to the
nature of the circuitry, so any existing missing battery status is not
removed here; this must be done externally. */
                for (i=0; i<NUM_BATS; i++)
                {
                    if (((getIndicators() >> 2*i) & 0x02) == 0)
                    {
                        battery[i].healthState = missingH;
                        setBatterySoC(i,0);
                    }
                }
/* Reset watchdog counter */
                monitorWatchdogCount = 0;
                for (i=0; i<NUM_IFS; i++) results[test][i] = getCurrent(i);
/* Send a progress update */
                dataMessageSendLowPriority("pQ",0,test);
            }

/* Estimate the offsets only when they are less than a threshold. Find the
lowest value for each interface. */
            for (i=0; i<NUM_IFS; i++)
            {
                currentOffsets.data[i] = OFFSET_START_VALUE;
/* Run through each test to find the minimum. This is the offset. */
                for (test=0; test<NUM_TESTS; test++)
                {
                    int16_t current = results[test][i];
/* Get the minimum if within the threshold. */
                    if (current > CALIBRATION_THRESHOLD)
                    {
                        if (current < currentOffsets.data[i])
                            currentOffsets.data[i] = current;
                    }
                }
/* If not changed, then the measurements were invalid, so set to zero. */
                if (currentOffsets.data[i] == OFFSET_START_VALUE)
                    currentOffsets.data[i] = 0;
/* Remove offset from the results */
                for (test=0; test<NUM_TESTS; test++)
                {
                    results[test][i] -= currentOffsets.data[i];
                }        
            }
/* Run through all tests and batteries to find the maximum. This is the
quiescent current */
            int16_t quiescentCurrent = -100;
            for (i=0; i<NUM_BATS; i++)
            {
                if (battery[i].healthState != missingH)
                {
                    for (test=0; test<NUM_TESTS; test++)
                    {
                        int16_t current = results[test][i];
/* Get the maximum if within threshold. */
                        if (current > CALIBRATION_THRESHOLD)
                        {
                            if (current > quiescentCurrent)
                                quiescentCurrent = current;
                        }
                    }
                }
            }
            dataMessageSendLowPriority("pQ",quiescentCurrent,7);

/* Restore switches and report back */
            setSwitchControlBits(switchSettings);
            dataMessageSendLowPriority("dS",switchSettings,0);
/* Compute the SoC from the OCV. Note the conditions for which OCV gives an
accurate estimate of SoC. */
/* Zero counters, and reset battery states */
            for (i=0; i<NUM_BATS; i++)
            {
                if (battery[i].healthState != missingH)
                {
                    setBatterySoC(i,computeSoC(getBatteryVoltage(i),
                                               getTemperature(),getBatteryType(i)));
                    battery[i].currentSteady = 0;
                    battery[i].isolationTime = 0;
                    battery[i].opState = isolatedO;
                }
            }
            batteryUnderLoad = 0;
            batteryUnderCharge = 0;
/* Write the offsets to FLASH */
            for (i=0; i<NUM_IFS; i++) setCurrentOffset(i,currentOffsets.data[i]);
            writeConfigBlock();
/* Ensure that calibration doesn't happen on the next cycle */
            calibrate = false;
        }

/*------------- RECORD AND REPORT STATE --------------*/
/**
<b>Record and Report State:</b> The current state variables are recorded
via the File module, and transmitted via the Communications module. */

/* Send off the current set of measurements */
        char id[4];
        id[0] = 'd';
        id[3] = 0;
/* Send out a time string */
        char timeString[20];
        putTimeToString(timeString);
        sendDebugString("pH",timeString);
        recordString("pH",timeString);
        uint8_t i;
        for (i=0; i<NUM_BATS; i++)
        {
            id[2] = '1'+i;
/* Send out battery terminal measurements. */
            id[1] = 'B';
            dataMessageSendLowPriority(id,
                        getBatteryCurrent(i),
                        getBatteryVoltage(i));
            recordDual(id,
                        getBatteryCurrent(i),
                        getBatteryVoltage(i));
/* Send out battery state of charge. */
            id[1] = 'C';
            sendResponseLowPriority(id,battery[i].SoC);
            recordSingle(id,battery[i].SoC);
/* Send out battery operational, fill, charging and health status indication. */
            uint16_t states = (battery[i].opState & 0x03) |
                             ((battery[i].fillState & 0x03) << 2) |
                             ((getBatteryChargingPhase(i) & 0x03) << 4) |
                             ((battery[i].healthState & 0x03) << 6);
            id[1] = 'O';
            sendResponseLowPriority(id,states);
            recordSingle(id,states);
        }
/* Send out load terminal measurements. */
        id[1] = 'L';
        for (i=0; i<NUM_LOADS; i++)
        {
            id[2] = '1'+i;
            dataMessageSendLowPriority(id,
                        getLoadCurrent(i)-getLoadCurrentOffset(i),
                        getLoadVoltage(i));
            recordDual(id,
                        getLoadCurrent(i)-getLoadCurrentOffset(i),
                        getLoadVoltage(i));
        }
/* Send out panel terminal measurements. */
        id[1] = 'M';
        for (i=0; i<NUM_PANELS; i++)
        {
            id[2] = '1'+i;
            dataMessageSendLowPriority(id,
                        getPanelCurrent(i)-getPanelCurrentOffset(i),
                        getPanelVoltage(i));
            recordDual(id,
                        getPanelCurrent(i)-getPanelCurrentOffset(i),
                        getPanelVoltage(i));
        }
/* Send out temperature measurement. */
        sendResponseLowPriority("dT",getTemperature());
        recordSingle("dT",getTemperature());
/* Send out control variables - isAutoTrack(), recording, calibrate */
        sendResponseLowPriority("dD",getControls());
        recordSingle("dD",getControls());
        sendResponseLowPriority("ds",(int)getSwitchControlBits());
        recordSingle("ds",(int)getSwitchControlBits());
/* Send switch and decision settings during tracking */
        if (isAutoTrack())
        {
            sendResponseLowPriority("dd",decisionStatus);
            recordSingle("dd",decisionStatus);
        }
/* Read the interface fault indicators and send out */
        sendResponseLowPriority("dI",getIndicators());
        recordSingle("dI",getIndicators());

/*------------- COMPUTE BATTERY STATE -----------------------*/
/**
<b>Compute the Battery State:</b>
<ul>
<li> Check to see if any batteries are missing and remove any existing loads
and charger from that battery. */
        for (i=0; i<NUM_BATS; i++)
        {
            if (battery[i].healthState == missingH)
            {
                setBatterySoC(i,0);
                if (batteryUnderLoad == i+1)
                    batteryUnderLoad = 0;
                if (batteryUnderCharge == i+1)
                    batteryUnderCharge = 0;
            }
        }
/* Find the number of batteries present */
        uint8_t numBats = NUM_BATS;
        for (i=0; i<NUM_BATS; i++)
        {
            if (battery[i].healthState == missingH) numBats--;
        }
/**
<li> Access charge accumulated for each battery since the last time, and update
the SoC. The maximum charge is the battery capacity in ampere seconds
(coulombs). */
        for (i=0; i<NUM_BATS; i++)
        {
            if (battery[i].healthState != missingH)
            {
                int16_t accumulatedCharge = getBatteryAccumulatedCharge(i);
                battery[i].charge += accumulatedCharge;
                uint32_t chargeMax = getBatteryCapacity(i)*3600*256;
                if (battery[i].charge < 0) battery[i].charge = 0;
                if ((uint32_t)battery[i].charge > chargeMax)
                    battery[i].charge = chargeMax;
                battery[i].SoC = battery[i].charge/(getBatteryCapacity(i)*36);

/* Collect the battery charge fill state indications. */
                uint16_t batteryAbsVoltage = abs(getBatteryVoltage(i));
                battery[i].fillState = normalF;
                if ((batteryAbsVoltage < configData.config.lowVoltage) ||
                    (battery[i].SoC < configData.config.lowSoC))
                    battery[i].fillState = lowF;
                else if ((batteryAbsVoltage < configData.config.criticalVoltage) ||
                         (battery[i].SoC < configData.config.criticalSoC))
                    battery[i].fillState = criticalF;
/**
<li> If a battery voltage falls below an absolute minimum dropout voltage, label
it as a weak battery to get the charger with priority and avoid loads. */
                if ((battery[i].healthState != missingH) &&
                    (batteryAbsVoltage < WEAK_VOLTAGE))
                {
                    battery[i].healthState = weakH;
                    battery[i].fillState = criticalF;
                    battery[i].SoC = 0;
                }
/**
<li> Restore good health to a battery when the it enters rest phase. This will
avoid thrashing when a battery is ailing. */
                if ((battery[i].healthState != missingH) &&
                    (getBatteryChargingPhase(i) == restC))
                {
                    battery[i].healthState = goodH;
                }
            }
        }
/**
<li> Rank the batteries by charge state. Bubble sort to have the highest SoC set
to the start of the list and the lowest at the end.
batteryFillStateSort has the values 1 ... NUM_BATS. */
        uint8_t k;
        uint16_t temp;
        uint8_t batteryFillStateSort[NUM_BATS];        
        for (i=0; i<NUM_BATS; i++) batteryFillStateSort[i] = i+1;
        for (i=0; i<NUM_BATS-1; i++)
        {
            for (k=0; k<NUM_BATS-i-1; k++)
            {
              if (battery[batteryFillStateSort[k]-1].SoC <
                  battery[batteryFillStateSort[k+1]-1].SoC)
              {
                temp = batteryFillStateSort[k];
                batteryFillStateSort[k] = batteryFillStateSort[k+1];
                batteryFillStateSort[k+1] = temp;
              }
            }
        }
/* Repeat bubble sort to get all missing batteries to the far end where they
will not be accessed. */
        for (i=0; i<NUM_BATS-1; i++)
        {
            for (k=0; k<NUM_BATS-i-1; k++)
            {
              if (battery[batteryFillStateSort[k]-1].healthState == missingH)
              {
                temp = batteryFillStateSort[k];
                batteryFillStateSort[k] = batteryFillStateSort[k+1];
                batteryFillStateSort[k+1] = temp;
              }
            }
        }
/**
<li> Find the batteries with the longest and shortest isolation times. */
        uint8_t longestBattery = 0;
        uint32_t longestTime = 0;
/*        uint8_t shortestBattery = 0;
        uint32_t shortestTime = 0xFFFFFFFF; */
        for (i=0; i<numBats; i++)
        {
            if ((battery[i].healthState != missingH) &&
                (battery[i].isolationTime > longestTime))
            {
                longestTime = battery[i].isolationTime;
                longestBattery = i+1;
            }
/*                if (battery[i].isolationTime < shortestTime)
            {
                shortestTime = battery[i].isolationTime;
                shortestBattery = i+1;
            }*/
        }
/** </ul> */

/*------------- BATTERY MANAGEMENT DECISIONS -----------------------*/
/**
<b>Battery Management Decisions:</b> Work through the battery states to allocate
loads to the highest SoC, accounting for batteries set to be isolated which we
wish to keep isolated if possible.
The first priority is to allocate the charger to a weak or critical battery.
The second priority is to allocate all loads to a "normal" charged battery even
if it must be taken out of isolation. Failing that, repeat for low state
batteries in order to hold onto the priority loads.
The charger, once allocated, is maintained until the charge algorithm releases
it or another battery becomes low. Decisions to change the charged battery are
therefore made when the charger is released (unallocated).
@note The code is valid for any number of batteries, but fixed for two loads and
one panel.
@todo update code for more panels (chargers) and loads. */
        uint8_t highestBattery = batteryFillStateSort[0];
        uint8_t lowestBattery = batteryFillStateSort[numBats-1];
/* decisionStatus is a debug variable used to record reasons for any decision */
        decisionStatus = 0;

/*------ PRELIMINARY DECISIONS ---------*/
/**
<b>Preliminary Decisions:</b>
<ul>
<li> Change each battery to bulk phase if it is in float phase and the SoC drops
below a charging restart threshold (default 95%). */
        for (i=0; i<numBats; i++)
        {
            uint8_t index = batteryFillStateSort[i];
            if ((battery[index].healthState != missingH) &&
                (getBatteryChargingPhase(index) == floatC) &&
                (getBatterySoC(index) < configData.config.floatBulkSoC))
                setBatteryChargingPhase(index,bulkC);
        }

/**
<li> If the currently allocated charging battery is in float or rest phase,
deallocate the charger to allow the algorithms to find another battery. */
        if (batteryUnderCharge > 0)
        {
            bool floatPhase =
                    (getBatteryChargingPhase(batteryUnderCharge-1) == floatC);
            bool restPhase =
                    (getBatteryChargingPhase(batteryUnderCharge-1) == restC);
            if (floatPhase || restPhase)
            {
                batteryUnderCharge = 0;
            }
        }

/**
<li> If the charging voltage drops below all of the the battery voltages,
turn off charging altogether. This will allow more flexibility in managing loads
and isolation during night periods. */
        chargerOff = true;
        for (i=0; i<numBats; i++)
        {
            uint8_t index = batteryFillStateSort[i];
            if ((battery[index].healthState != missingH) &&
                (getBatteryVoltage(index) < (getPanelVoltage(0)+128)))
            {
                decisionStatus |= 0x100;
                chargerOff = false;
                break;
            }
        }
        if (chargerOff) batteryUnderCharge = 0;

/**
<li> If all batteries are in float phase, disconnect the charger.
</ul> */
        bool allInFloat = true;
        for (i=0; i<numBats; i++)
        {
            uint8_t index = batteryFillStateSort[i];
            if ((battery[index].healthState != missingH) &&
                (getBatteryChargingPhase(index) != floatC))
            {
                allInFloat = false;
                break;
            }
        }
        if (allInFloat)
        {
            decisionStatus |= 0x200;
            chargerOff = true;
            batteryUnderCharge = 0;
        }

/*------ ONE BATTERY ---------*/
/**
<b>One Battery:</b>
<ul>
<li> Just allocate the loads and charger to it. */
        if (numBats == 1)
        {
            uint8_t i;
            uint8_t index = batteryFillStateSort[0];
            decisionStatus |= 0x1000;
            batteryUnderCharge = index;
            batteryUnderLoad = index;
/**
<li> If the loaded battery is weak, then deallocate the loads to the battery.
</ul> */
            if (battery[index].healthState == weakH)
            {
                decisionStatus |= 0x40;
                batteryUnderLoad = 0;
            }
        }

/*------ MULTIPLE BATTERIES ---------*/
/**
<b>More than one battery:</b>
<ul>
<li> Allocate the loads to the highest and the charger
to the lowest, taking into account the battery health and if the charger is
active, and for more than two batteries, whether one may be kept isolated. */
        else if (numBats > 1)
        {
            decisionStatus |= 0x2000;

/**
<li> For more than two batteries, at least one can be held isolated for later
determination of reasonably accurate terminal voltage and hence SoC.
</ul> */
            bool isolatable = (numBats > 2);

/* CHARGER allocation. */
/**
<b> Charger allocation: </b>
*/
            if (! chargerOff)
            {
/**
<ul>
<li> If the lowest battery is not in normal state, deallocate the charger
to allow it to be moved to another battery if necessary. */
                if (battery[lowestBattery-1].fillState != normalF)
                    batteryUnderCharge = 0;
/**
<li> If the lowest battery is critical, allocate the charger unconditionally. */
                if (battery[lowestBattery-1].fillState == criticalF)
                {
                    batteryUnderCharge = lowestBattery;
                    decisionStatus |= 0x08;
                }
/**
<li> Check all batteries in case there is a weak one requiring the charger with
priority. This will only deal with the lowest SoC such battery encountered. */
                for (i=0; i<numBats; i++)
                {
                    uint8_t index = batteryFillStateSort[numBats-i-1];
                    if (battery[index-1].healthState == weakH)
                    {
                        batteryUnderCharge = index;
                        decisionStatus |= 0x04;
                        break;
                    }
                }
/**
<li> If the charger is unallocated, set to the lowest SoC battery, provided this
battery is not in float with SoC > 95%, nor in rest phase, nor isolated. */
                if ((batteryUnderCharge == 0) && isolatable)
                {
                    for (i=0; i<numBats; i++)
                    {
                        uint8_t index = batteryFillStateSort[numBats-i-1];
                        bool floatPhase =
                                (getBatteryChargingPhase(index-1) == floatC);
                        bool restPhase =
                                (getBatteryChargingPhase(index-1) == restC);
                        bool isolated = ((index == longestBattery) &&
                                    (getMonitorStrategy() & PRESERVE_ISOLATION));
                        if (!floatPhase && !restPhase && !isolated)
                        {
                            decisionStatus |= 0x01;
                            batteryUnderCharge = index;
                            break;
                        }
                    }
                }
/**
<li> If the charger is still unallocated, don't worry if a battery is
isolated. This is also activated if the system is not isolatable. The charger
is not allocated if all are in rest or float. */
                if (batteryUnderCharge == 0)
                {
                    for (i=0; i<numBats; i++)
                    {
                        uint8_t index = batteryFillStateSort[numBats-i-1];
                        bool floatPhase =
                                (getBatteryChargingPhase(index-1) == floatC);
                        bool restPhase =
                                (getBatteryChargingPhase(index-1) == restC);
                        if (!floatPhase && !restPhase)
                        {
                            decisionStatus |= 0x02;
                            batteryUnderCharge = index;
                            break;
                        }
                    }
                }
/**
<li> If the battery under charge ends up on a good battery, check again in case
there is a lower battery and don't worry if that battery is isolated. */
                if ((batteryUnderCharge != 0) &&
                    (battery[batteryUnderCharge-1].fillState == normalF))
                {
                    for (i=0; i<numBats; i++)
                    {
                        uint8_t index = batteryFillStateSort[numBats-i-1];
                        bool floatPhase =
                                (getBatteryChargingPhase(index-1) == floatC);
                        bool restPhase =
                                (getBatteryChargingPhase(index-1) == restC);
                        bool better = (battery[batteryUnderCharge-1].SoC >
                                                battery[index-1].SoC+5*256);
                        if (!floatPhase && !restPhase && better)
                        {
                            batteryUnderCharge = index;
                            decisionStatus |= 0x03;
                            break;
                        }
                    }
                }
            }

/* LOAD allocation */
/**
</ul>
<b> Load allocation: </b>
<ul>
<li> If the charger has been allocated to the loaded battery, then deallocate
the loaded battery. This will allow the charger to swap back and forth as the
loaded battery droops and the charging battery completes charge. If the charger
has been deallocated, maintain the load on the same battery as long as it is
still in normal state. */
            if ((batteryUnderLoad == batteryUnderCharge) &&
                (getMonitorStrategy() & SEPARATE_LOAD))
                batteryUnderLoad = 0;

/**
<li> If the loaded battery is weak, then deallocate the loaded battery. */
            if ((batteryUnderLoad != 0) && 
                (battery[batteryUnderLoad-1].healthState == weakH))
                batteryUnderLoad = 0;

/**
<li> If the battery under load is on a low or critical battery, deallocate
to allow a better battery to be sought. */
            if ((batteryUnderLoad != 0) &&
                (battery[batteryUnderLoad-1].fillState != normalF))
                batteryUnderLoad = 0;
/**
<li> If the loads are unallocated, set to the highest SoC unallocated battery.
Avoid the battery that has been idle for the longest time, and also the battery
under charge if the strategies require it.  Do not allocate if the health state
is weak. A weak battery will have the charger allocated already. This will only
leave without the load allocated if all batteries are weak. */
            if ((batteryUnderLoad == 0) && isolatable)
            {
                for (i=0; i<numBats; i++)
                {
                    uint8_t index = batteryFillStateSort[i];
                    bool isolated = ((index == longestBattery) &&
                                    (getMonitorStrategy() & PRESERVE_ISOLATION));
                    bool charging = ((index == batteryUnderCharge) &&
                                    (getMonitorStrategy() & SEPARATE_LOAD));
                    bool weak = (battery[index-1].healthState == weakH);
                    if (!weak && !isolated && !charging)
                    {
                        batteryUnderLoad = index;
                        decisionStatus |= 0x10;
                        break;
                    }
                }
            }
/**
<li> If still not allocated, just go for a battery that is not weak and not on
the charger, and don't worry if battery is isolated. This is also activated
if the system is not isolatable. */
            if (batteryUnderLoad == 0)
            {
                for (i=0; i<numBats; i++)
                {
                    uint8_t index = batteryFillStateSort[i];
                    bool charging = ((index == batteryUnderCharge) &&
                                    (getMonitorStrategy() & SEPARATE_LOAD));
                    bool weak = (battery[index-1].healthState == weakH);
                    if (!weak && !charging)
                    {
                        batteryUnderLoad = index;
                        decisionStatus |= 0x20;
                        break;
                    }
                }
            }
/**
<li> If still not allocated, just go for a battery that is not weak. If this is
still not found, loads will remain unallocated. */
            if (batteryUnderLoad == 0)
            {
                for (i=0; i<numBats; i++)
                {
                    uint8_t index = batteryFillStateSort[i];
                    bool weak = (battery[index-1].healthState == weakH);
                    if (!weak)
                    {
                        batteryUnderLoad = index;
                        decisionStatus |= 0x40;
                        break;
                    }
                }
            }
/**
<li> If the battery under load ends up on a low or critical battery, look again
for a better battery that is not weak and not on the charger, and don't worry if
that battery is isolated. */
            if ((batteryUnderLoad != 0) &&
                (battery[batteryUnderLoad-1].fillState != normalF))
            {
                for (i=0; i<numBats; i++)
                {
                    uint8_t index = batteryFillStateSort[i];
                    bool charging = ((index == batteryUnderCharge) &&
                                    (getMonitorStrategy() & SEPARATE_LOAD));
                    bool weak = (battery[index-1].healthState == weakH);
                    bool better = (battery[batteryUnderCharge-1].SoC >
                                            battery[index-1].SoC+5*256);
                    if (!weak && !charging && better)
                    {
                        batteryUnderLoad = index;
                        decisionStatus |= 0x30;
                        break;
                    }
                }
            }
/**
<li> If the battery under load still ends up on a critical battery, allocate to
the charging battery regardless of strategies, if the latter is not also weak. */

            if ((batteryUnderCharge != 0) && (batteryUnderLoad != 0) &&
                (battery[batteryUnderCharge-1].healthState != weakH) &&
                (battery[batteryUnderLoad-1].fillState == criticalF))
            {
                batteryUnderLoad = batteryUnderCharge;
                decisionStatus |= 0x80;
            }
        }
/** </ul> */
/*--------------END BATTERY MANAGEMENT DECISIONS ------------------*/

/**
<b>Global Decisions:</b>
<ul>
<li> Compute any changes in battery operational states. */
        for (i=0; i<NUM_BATS; i++)
        {
            uint8_t lastOpState = battery[i].opState;
            if (battery[i].healthState != missingH)
            {
                battery[i].opState = isolatedO; /* reset operational state */
                if ((batteryUnderLoad > 0) && (batteryUnderLoad == i+1))
                {
                    battery[i].opState = loadedO;
                }
                if ((batteryUnderCharge > 0) && (i == batteryUnderCharge-1))
                {
                    battery[i].opState = chargingO;
                }

/**
<li> If the operational state of a battery changes from isolated, update the SoC
if it has been isolated for over 4 hours */
                if ((lastOpState == isolatedO) &&
                    (battery[i].opState != isolatedO) &&
                    (battery[i].isolationTime >
                                    (uint32_t)(4*3600*1024)/getMonitorDelay()))
                {
                    setBatterySoC(i,computeSoC(getBatteryVoltage(i),
                                           getTemperature(),getBatteryType(i)));
                    battery[i].isolationTime = 0;
                }
/**
<li> Restart the isolation timer for the battery if it is not isolated or if the
charger and loads are on the same battery (isolation is not possible in that
case due to leakage of charging current to other batteries). Set the timer to a
low value rather than down completely to zero, so that the currently allocated
isolation timer can handover later.
</ul> */
                if ((battery[i].opState != isolatedO) ||
                    (batteryUnderLoad == batteryUnderCharge))
                    battery[i].isolationTime = 10;
            }
        }
        if (isAutoTrack())
        {
/**
<b> Set Load Switches. </b>
<ul>*/
            setSwitch(batteryUnderLoad,LOAD_2);
/**
<li> Turn off all low priority loads if the batteries are all critical */
            if (battery[batteryUnderLoad-1].fillState == criticalF)
            {
                setSwitch(0,LOAD_1);
            }
            else
            {
                setSwitch(batteryUnderLoad,LOAD_1);
            }
/**
<li> Connect the battery under charge to the charger if the temperature is below
the high temperature limit, otherwise leave it unconnected. */
            if (getTemperature() < TEMPERATURE_LIMIT*256)
                setSwitch(batteryUnderCharge,PANEL);
/**
<li> Set the battery selected for charge as the "preferred" battery so that it
continues to be used if autotrack is turned off. This information is passed to
the charger task.
</ul> */
            setPanelSwitchSetting(batteryUnderCharge);
        }

/*---------------- RESET SoC AFTER IDLE TIME --------------------*/
/**
<b> Reset the SoC after a programmed isolation time. </b>
<ul>
<li> Compute the state of charge estimates from the open circuit voltage (OCV)
if the currents are low for the selected time period. The steady current
indicator is incremented on each cycle that the current is below a threshold of
about 80mA. */
        uint32_t monitorHour = (uint32_t)(3600*1000)/getMonitorDelay();
        for (i=0; i<NUM_BATS; i++)
        {
            if (battery[i].healthState != missingH)
            {
                if (abs(getBatteryCurrent(i)) < 30)
                    battery[i].currentSteady++;
                else
                    battery[i].currentSteady = 0;
                if (battery[i].currentSteady > monitorHour)
                {
                    setBatterySoC(i,computeSoC(getBatteryVoltage(i),
                                               getTemperature(),getBatteryType(i)));
                    battery[i].currentSteady = 0;
                }
/**
<li> Update the isolation time of each battery. If a battery has been isolated
for over 8 hours, compute the SoC and drop the isolation time back to zero to
allow other batteries to pass to the isolation state. */
                battery[i].isolationTime++;
                if (battery[i].isolationTime > 8*monitorHour)
                {
                    setBatterySoC(i,computeSoC(getBatteryVoltage(i),
                                               getTemperature(),getBatteryType(i)));
                    battery[i].isolationTime = 0;
                }
            }
        }

/**
</ul> */

/* Wait until the next tick cycle */
        vTaskDelay(getMonitorDelay());
/* Reset watchdog counter */
        monitorWatchdogCount = 0;
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Initialise Global Variables to Defaults

*/

static void initGlobals(void)
{
    calibrate = false;
    uint8_t i=0;
    for (i=0; i<NUM_BATS; i++)
    {
/* Determine capacity */
        setBatterySoC(i,computeSoC(getBatteryVoltage(i),getTemperature(),
                                   getBatteryType(i)));
        battery[i].currentSteady = 0;
        battery[i].isolationTime = 0;
/* Start with all batteries isolated */
        battery[i].opState = isolatedO;
        battery[i].healthState = goodH;
    }
    batteryUnderLoad = 0;
    batteryUnderCharge = 0;
/* Load the currrent offsets to the local structure. These will be in FLASH,
or will be set to zero if not. */
    for (i=0; i<NUM_IFS; i++) currentOffsets.data[i] = getCurrentOffset(i);
}

/*--------------------------------------------------------------------------*/
/** @brief Compute SoC from OC Battery Terminal Voltage and Temperature

This model covers the Gel and Wet cell batteries.
Voltage is referred to the value at 48.9C so that one table can be used.
Refer to documentation for the model formula derived.

@param voltage: uint32_t Measured open circuit voltage. Volts times 256
@param temperature: uint32_t Temperature degrees C times 256
@param type: battery_Type
@return int16_t Percentage State of Charge times 256.
*/

int16_t computeSoC(uint32_t voltage, uint32_t temperature, battery_Type type)
{
    int32_t soc;
    int32_t v100, v50, v25;
    if (type == wetT)
        v100 = 3242;                /* 12.66 */
    else v100 = 3280;               /* 12.81 */
/* Difference between top temperature 48.9C and ambient, times 64. */
    uint32_t tDiff = (12518-temperature) >> 2;
/* Correction factor to apply to measured voltages, times 65536. */
    uint32_t vFactor = 65536-((42*tDiff*tDiff) >> 20);
/* Open circuit voltage referred to 48.9C */
    int32_t ocv = (voltage*65536)/vFactor;
/* SoC for Wet cell and part of Gel cell */
    soc = 100*(65536 - 320*(v100-ocv));
/* Ca/Ca battery types */
    if ((type == gelT) || (type == agmT))
    {
/* Change slope for low SoC values as per documentation */
        v50 = 3178;                 /* 12.41 */
        if (ocv < v50)
        {
            v25 = 3075;             /* 12.01 */
            if (ocv > v25) soc = soc + 100*160*(v50-ocv);
            else soc = soc + 100*160*(v50-v25);
        }
    }
    soc = (soc >> 8);               /* Adjust back from 65536 to 256 scaling.*/
    if (soc > 100*256) soc = 100*256;
    if (soc < 0) soc = 0;
    return soc;
}

/*--------------------------------------------------------------------------*/
/** @brief Access the Battery Current Offset

@param[in] i: int 0..NUM_BATS-1
@return int16_t battery current offset.
*/

int16_t getBatteryCurrentOffset(int i)
{
    return currentOffsets.dataArray.battery[i];
}

/*--------------------------------------------------------------------------*/
/** @brief Access the Load Current Offset

@param[in] battery: int 0..NUM_LOADS-1
@return int16_t battery current offset.
*/

int16_t getLoadCurrentOffset(int load)
{
    return currentOffsets.dataArray.load[load];
}

/*--------------------------------------------------------------------------*/
/** @brief Access the Panel Current Offset

@param[in] battery: int 0..NUM_PANELS-1
@return int16_t battery current offset.
*/

int16_t getPanelCurrentOffset(int panel)
{
    return currentOffsets.dataArray.panel[panel];
}

/*--------------------------------------------------------------------------*/
/** @brief Access the Battery Health State

@param[in] battery: int 0..NUM_BATS-1
@return int16_t battery Health State.
*/

battery_Hl_States getBatteryHealthState(int i)
{
    return battery[i].healthState;
}

/*--------------------------------------------------------------------------*/
/** @brief Access the Battery State of Charge

@param[in] battery: int 0..NUM_BATS-1
@return int16_t battery SoC.
*/

int16_t getBatterySoC(int i)
{
    return battery[i].SoC;
}

/*--------------------------------------------------------------------------*/
/** @brief Get the Battery Under Load

@return int16_t battery under load.
*/

int16_t getBatteryUnderLoad(void)
{
    return batteryUnderLoad;
}

/*--------------------------------------------------------------------------*/
/** @brief Set the Battery Under Load

@param[in] i: int 0..NUM_BATS-1
*/

void setBatteryUnderLoad(int i)
{
    batteryUnderLoad = i;
}

/*--------------------------------------------------------------------------*/
/** @brief Reset the Battery State of Charge to 100%

This is done by the charging task when the battery enters float phase.
If the current SoC is less than 100%, report the battery as faulty.

@param[in] i: int 0..NUM_BATS-1
*/

void resetBatterySoC(int i)
{
    if (battery[i].SoC < 25600) battery[i].fillState = faultyF;
    setBatterySoC(i,25600);
}

/*--------------------------------------------------------------------------*/
/** @brief Set the Battery State of Charge

State of charge is percentage times 256. The accumulated charge is also computed
here in ampere seconds.

@param[in] i: int 0..NUM_BATS-1
@param[in] soc: int16_t 0..25600
*/

void setBatterySoC(int i,int16_t soc)
{
    if (battery[i].SoC > 25600) battery[i].SoC = 25600;
    else battery[i].SoC = soc;
/* SoC is computed from the charge so this is the quantity changed. */
    battery[i].charge = soc*getBatteryCapacity(i)*36;
}

/*--------------------------------------------------------------------------*/
/** @brief Request a Calibration Sequence

*/

void startCalibration()
{
    calibrate = true;
}

/*--------------------------------------------------------------------------*/
/** @brief Change Missing Status of batteries

@param[in] i: int 0..NUM_BATS-1
@param[in] missing: boolean
*/

void setBatteryMissing(int i, bool missing)
{
    if (missing) battery[i].healthState = missingH;
    else battery[i].healthState = goodH;
}

/*--------------------------------------------------------------------------*/
/** @brief Check the watchdog state

The watchdog counter is decremented. If it reaches zero then the task is
restarted.
*/

void checkMonitorWatchdog(void)
{
    if (monitorWatchdogCount++ > 10*getMonitorDelay()/getWatchdogDelay())
    {
        vTaskDelete(prvMonitorTask);
        xTaskCreate(prvMonitorTask, (portCHAR * ) "Monitor", \
                    configMINIMAL_STACK_SIZE, NULL, MONITOR_TASK_PRIORITY, NULL);
        sendDebugString("D","Monitor Restarted");
        recordString("D","Monitor Restarted");
    }
}

/*--------------------------------------------------------------------------*/

/**@}*/

