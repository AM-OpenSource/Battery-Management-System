Battery Management System Firmware
----------------------------------

Version: 1.0.0

The firmware is written for an STM32F103 ARM Cortex M3 microcontroller and uses
specific hardware published here.

The firmware uses libopencm3 for hardware drivers, FreeRTOS for task management
and ChaN FatFs for managing the local storage filesystem, along with an
adaption of the driver code by Martin Thomas for SDSC/MMC SPI drivers to
incorporate libopencm3 and FreeRTOS.

Several tasks are defined to manage:

- command and data communications,
- storage,
- battery state monitoring,
- charge control,
- measurement,
- program execution watchdog and error recovery.

It is designed to be adapted to allow CANopen to manage communications with an
external PC, in case the project is further developed.

The hardware component values set the various scaling factors for voltages and
currents, and these may change for different hardware versions.

Three versions are here represented. Each refers to slightly different
voltage and current scales in the measurement header for the interface PCB
version. Currently version 3 is being used and is the default.
It is compiled with 'make VERSION=x' where x = 2 or 3. Version 1 is obsolete.

The charger algorithm is referred to as "Pulse Charge". This aims to avoid the
PWM switching needed to maintain a constant absorption phase voltage, which
reduces EMI problems and also reduces overcharging by keeping the battery charge
voltages as low as possible. The Pulse Charge algorithm is an adaptation of 
the Interrupted Charge Control, Intermittent Charge, and Three Phase algorithms
reported in the literature, providing efficient and independent charging of
multiple batteries from an uncontrolled variable power source such as that found
in solar and aeolian systems.

Compilation is done on Linux using The arm-none-eabi compiler package and by
command line "make" using a makefile. The makefile needs to be modified to suit
the installation of libraries: change the macro LIBRARY_DIR for libopencm3, and
FREERTOS_DIR for FreeRTOS.

The ChaN FAT library is provided as it has inbuilt modifications to allow use
with libopencm3. If ChaN FAT is upgraded, the file sd_spi_loc3_stm32_freertos.c
must be retained (or adapted as necessary).

More information is available on [Jiggerjuice](http://www.jiggerjuice.info/electronics/projects/solarbms/solarbms-software.html)

(c) K. Sarkies 24/05/2016

BUGS

1. The file directory list command results in very slow transmission, about one
   entry per second.
2. File dates are sometimes wrong.
3. Auto tracker often makes odd decisions, such as placing load on a low battery
   or charging a battery of higher charge.

TODO

1. Firmware upload - from within the program. Base on stm32flash.
2. Comms - test queue space available for each data message to send.
3. File - add file info (date) to GUI.
4. Measurement - Increase switching speed to 25kHz. May need hardware changes.

BUGS


