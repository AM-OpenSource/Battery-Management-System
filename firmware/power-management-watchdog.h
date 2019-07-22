/* STM32F1 Power Management for Solar Power

This header file contains defines and prototypes specific to the charging
task.

Initial 15 March 2014
21 July 2019 Added task starter function
*/

/*
 * This file is part of the battery-management-system project.
 *
 * Copyright 2014 K. Sarkies <ksarkies@internode.on.net>
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

#ifndef POWER_MANAGEMENT_WATCHDOG_H_
#define POWER_MANAGEMENT_WATCHDOG_H_

/*--------------------------------------------------------------------------*/
/* Prototypes */
/*--------------------------------------------------------------------------*/
void prvWatchdogTask(void *pvParameters);
void startWatchdogTask(void);

#endif

