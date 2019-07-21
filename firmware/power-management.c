/**
@mainpage Battery Power Management for Solar Power System
@version 1.0.0
@author Ken Sarkies (www.jiggerjuice.info)
@date 12 November 2016

@brief Management of solar battery charger and battery state monitor.

The power management system provides interface current, voltage (analogue),
overcurrent and undervoltage (digital) signals. This program measures these
quantities, stores and transmits them to an external PC.

Three batteries, two loads and a single solar module (panel) are provided.

A number of tasks are activated to manage the charge/discharge of the batteries
and to deal with events such as overloads or undervoltages. In addition
a command interface is established for external override controls.

The program estimates State of Charge (SoC) of each of the batteries and
tracks it using Coulomb Counting. The SoC is reset whenever the batteries
are idle for a significant period of time.

FreeRTOS provides the real time operating system.

It was intended that CANopen provide the smart transducer communications through
CanFestival, but this has not been implemented fully.

The board used is the ET-STM32F103 (RBT6).
(On this board, ADC1 channels 2 and 3 (PA2, PA3) are shared with USART2 and two
jumpers J13,J14 need to be moved to access the analogue ports. Also PA6-8 are
used by the SPI1 for the MMIC card interface, so the latter cannot be used.)

Hardware using the ET-ARM-STAMP (RET6) has been completed and is permanently in
use.

Initial 13 July 2013

FreeRTOS 9 August 2013

10/11/2016: FreeRTOS v 9.0.0

10/11/2016: libopencm3 commit 011b5c615ad398bd14bbc58e43d9b3335cfaa1b8

10/11/2016: ChaN FatFS R0.12b
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

#include <stdint.h>
#include <stdbool.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "power-management-hardware.h"
#include "power-management-objdic.h"
#include "power-management-time.h"
#include "power-management-watchdog.h"
#include "power-management-comms.h"
#include "power-management-file.h"
#include "power-management-measurement.h"
#include "power-management-monitor.h"
#include "power-management-charger.h"
#include "power-management.h"

/*--------------------------------------------------------------------------*/
/* @brief Main Program

The hardware and communications are initialised then all tasks are launched. */

int main(void)
{
    setGlobalDefaults();        /* From objdic */
    prvSetupHardware();         /* From hardware */
    initComms();                /* From comms */

/* Start the watchdog task. */
    startWatchdogTask();

/* Start the communications task. */
    startCommunicationsTask();

/* Start the file management task. */
    startFileTask();

/* Start the measurement task. */
    startMeasurementTask();

/* Start the monitor task. */
    startMonitorTask();

/* Start the charger task. */
    startChargerTask();

/* Start the scheduler. */
    vTaskStartScheduler();

/* Will only get here if there was not enough heap space to create the
idle task. */
    return -1;
}


