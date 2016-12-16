/** @defgroup Comms_file Communications

@brief Communications Task and library

This task receives incoming characters and assembles them into a command line.
The command is then interpreted and executed.
Commands include file operations, switch settings, all parameter settings.
The library consists of all routines needed by other tasks to communicate.

The communication medium is non-specific, referring on to the hardware module.

This task is the only one having write access to the object dictionary items.

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

/**@{*/

#include <stdint.h>
#include <stdbool.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include "power-management-hardware.h"
#include "power-management-objdic.h"
#include "power-management-file.h"
#include "power-management-measurement.h"
#include "power-management-monitor.h"
#include "power-management-charger.h"
#include "power-management-time.h"
#include "power-management-lib.h"
#include "power-management-comms.h"
#include "ff.h"

/*--------------------------------------------------------------------------*/
/* Local Prototypes */
static void initGlobals(void);
static void parseCommand(uint8_t* line);
static void resetCallback(xTimerHandle resethandle);
static void lapseCommsCallback(xTimerHandle lapseCommsTimer);
static void commsPrintInt(int32_t value);
static void commsPrintHex(uint32_t value);
static void commsPrintString(char *ch);
static void commsPrintChar(char *ch);

/*--------------------------------------------------------------------------*/
/* Global Variables */
/* These configuration variables are part of the Object Dictionary. */
/* This is defined in power-management-objdic and is updated in response to
received messages. */
extern union ConfigGroup configData;

/* FreeRTOS queues and intercommunication variables */
/* Serial Comms defined here */
/* The semaphore must be used to protect messages until they have been queued
in their entirety. This is done in convenience functions defined below */
xQueueHandle commsSendQueue, commsReceiveQueue;
xSemaphoreHandle commsSendSemaphore, commsEmptySemaphore;
/* FreeRTOS queue to receive command responses, defined in File */
extern xQueueHandle fileReceiveQueue;
extern xSemaphoreHandle fileSendSemaphore;

/*--------------------------------------------------------------------------*/
/* Local Variables */
static uint32_t intf;
static char writeFileName[12];
static char readFileName[12];
static uint8_t writeFileHandle;
static uint8_t readFileHandle;
static int lapseCommsID;
static xTimerHandle lapseCommsTimer;

/*--------------------------------------------------------------------------*/
/** @brief Communications Receive Task

This collects characters received over the communications interface and packages
them for action as a command.
*/

void prvCommsTask(void *pvParameters)
{
    pvParameters = pvParameters;

    static uint8_t line[80];
    static uint8_t characterPosition = 0;

    initGlobals();

/* Timer to cause outgoing communications to cease if nothing received for 10
seconds */
    lapseCommsTimer = xTimerCreate("Lapse Comms",10000,pdFALSE,0,lapseCommsCallback);

    while(1)
    {
/* Build a command line string before actioning. The task will block
indefinitely waiting for input. */
        char character;
        xQueueReceive(commsReceiveQueue,&character,portMAX_DELAY);
        if ((character == 0x0D) || (character == 0x0A) || (characterPosition > 78))
        {
            if (lapseCommsTimer != NULL) xTimerReset(lapseCommsTimer,0);
            line[characterPosition] = 0;
            characterPosition = 0;
            parseCommand(line);
        }
        else line[characterPosition++] = character;
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Initialize

This initializes the queues and semaphores used by the task.
*/

void initComms(void)
{
/* Setup the queues to use */
    commsSendQueue = xQueueCreate(COMMS_QUEUE_SIZE,1);
    commsReceiveQueue = xQueueCreate(COMMS_QUEUE_SIZE,1);
    commsSendSemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(commsSendSemaphore);
    commsEmptySemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(commsEmptySemaphore);
}

/*--------------------------------------------------------------------------*/
/** @brief Initialise Global Variables

*/

static void initGlobals(void)
{
/* File name and handle variables are carried globally from the time of creation */
    writeFileName[0] = 0;
    readFileName[0] = 0;
    writeFileHandle = 0xFF;
    readFileHandle = 0xFF;
}

/*--------------------------------------------------------------------------*/
/** @brief Parse a command line and act on it.

Commands to and from the BMS are single line ASCII text strings consisting of
a category character (a=action, d=data request, p=parameter, f=file) followed
by an upper case command character and an arbitrary length set of parameters
(limited to 80 character line length in total).

Unrecognizable messages are just discarded.

@param[in] line: uint8_t* pointer to the command line in ASCII
*/

static void parseCommand(uint8_t* line)
{
/* ======================== Action commands ========================  */
/**
<b>Action Commands</b> */
    if (line[0] == 'a')
    {
/**
<ul>
<li> <b>Snm</b> Manually set Switch. Follow by battery n (1-3, 0 = none) and
load m (0-1)/panel (2). Each two-bit field represents a load or panel, and the
setting is the battery to be connected (no two batteries can be connected to a
load/panel). */
        switch (line[1])
        {
        case 'S':
            {
                uint8_t battery = line[2]-'0';
                uint8_t setting = line[3]-'0'-1;
                if ((battery < 4) && (setting < 4)) setSwitch(battery, setting);
                if (setting == 2) setPanelSwitchSetting(battery);
                break;
            }
/**
<li> <b>Rn</b> Reset a tripped overcurrent circuit breaker.
Set a FreeRTOS timer to expire after 250ms at which time the reset line is
released. The command is followed by an interface number n=0-5 being batteries
1-3, loads 1-2 and module. */
        case 'R':
            {
                portTickType resetTime = 250;
                intf = line[2]-'0';
                if (intf > NUM_IFS-1) break;
                xTimerHandle resetHandle
                    = xTimerCreate("Reset",resetTime,pdFALSE,(void *)intf,resetCallback);
                if(resetHandle == NULL) break;
                if (xTimerStart(resetHandle,0) != pdPASS) break;
                overCurrentReset(intf);
                break;
            }
/**
<li> <b>W</b> Write the current configuration block to FLASH */
        case 'W':
            {
                writeConfigBlock();
                break;
            }
/**
<li> <b>E</b> Send an ident response */
        case 'E':
            {
                char ident[35] = "Battery Management System,";
                stringAppend(ident,FIRMWARE_VERSION);
                stringAppend(ident,",");
                char version[3];
                intToAscii(VERSION,version);
                stringAppend(ident,version);
                sendStringLowPriority("dE",ident);
                break;
            }
/**
<li> <b>B</b> Set the battery SoC from the measured OCV */
        case 'B':
            {
                uint8_t battery = line[2]-'1';
                setBatterySoC(battery,computeSoC(getBatteryVoltage(battery),
                           getTemperature(),getBatteryType(battery)));
                break;
            }
        }
    }
/**
</ul> */
/* ======================== Data request commands ================  */
/**
<b>Data Request Commands</b> */
    else if (line[0] == 'd')
    {
        switch (line[1])
        {
/**
<ul>
<li> <b>S</b> Ask for all switch settings to be sent as well as control
settings. */
        case 'S':
            {
                sendResponse("dS",(int)getSwitchControlBits());
                uint8_t controlByte = getControls();
                sendResponse("dD",controlByte);
                break;
            }
/**
<li> <b>Bn</b> Ask for battery n=1-3 parameters to be sent */
        case 'B':
            {
                char id[] = "pR0";
                id[2] = line[2];
                uint8_t battery = line[2] - '1';
                dataMessageSend(id,getBatteryResistanceAv(battery),0);
                id[1] = 'T';
                dataMessageSend(id,(int32_t)configData.config.batteryType[battery],
                                   (int32_t)configData.config.batteryCapacity[battery]);
                id[1] = 'F';
                dataMessageSend(id,(int32_t)configData.config.
                                            floatStageCurrentScale[battery],
                                   (int32_t)configData.config.floatVoltage[battery]);
                id[1] = 'A';
                dataMessageSend(id,(int32_t)configData.config.
                                            bulkCurrentLimitScale[battery],
                                   (int32_t)configData.config.absorptionVoltage[battery]);
                break;
            }
/**
<li> <b>T</b> Ask for monitor strategy parameters to be sent. */
        case 'T':
            {
                char id[] = "pts";
                dataMessageSend(id,(int32_t)configData.config.monitorStrategy,0);
                id[2] = 'V';
                dataMessageSend(id,(int32_t)configData.config.lowVoltage,
                                   (int32_t)configData.config.criticalVoltage);
                id[2] = 'S';
                dataMessageSend(id,(int32_t)configData.config.lowSoC,
                                   (int32_t)configData.config.criticalSoC);
                id[2] = 'F';
                dataMessageSend(id,(int32_t)configData.config.floatBulkSoC,0);
                break;
            }
/**
<li> <b>C</b> Ask for charger strategy parameters to be sent. */
        case 'C':
            {
                char id[] = "pcs";
                dataMessageSend(id,(int32_t)configData.config.chargerStrategy,0);
                id[2] = 'R';
                dataMessageSend(id,(int32_t)configData.config.restTime,
                                   (int32_t)configData.config.absorptionTime);
                id[2] = 'D';
                dataMessageSend(id,(int32_t)configData.config.minDutyCycle,0);
                id[2] = 'F';
                dataMessageSend(id,(int32_t)configData.config.floatTime,
                                   (int32_t)configData.config.floatBulkSoC);
                break;
            }
        }
    }
/**
</ul> */
/* ================ Parameter Setting commands ================  */
/**
<b>Parameter Setting Commands</b> */
    else if (line[0] == 'p')
    {
        uint8_t battery = line[2]-'1';
        switch (line[1])
        {
/**
<ul>
<li> <b>a-, a+</b> Turn autoTracking on or off */
        case 'a':
            {
                if (line[2] == '-') configData.config.autoTrack = false;
                else if (line[2] == '+') configData.config.autoTrack = true;
                break;
            }
/**
<li> <b>c-, c+</b> Turn communications sending on or off */
        case 'c':
            {
                if (line[2] == '-') configData.config.enableSend = false;
                else if (line[2] == '+') configData.config.enableSend = true;
                break;
            }
/**
<li> <b>C</b> Start a calibration sequence */
        case 'C':
            {
                startCalibration();
                break;
            }
/**
<li> <b>d-, d+</b> Turn on debug messages */
        case 'd':
            {
                if (line[2] == '+') configData.config.debugMessageSend = true;
                if (line[2] == '-') configData.config.debugMessageSend = false;
                break;
            }
/**
<li> <b>Hxxxx</b> Set time from an ISO 8601 formatted string. */
        case 'H':
            {
                setTimeFromString((char*)line+2);
                break;
            }
/**
<li> <b>M-, M+</b> Turn on/off data messaging (mainly for debug) */
        case 'M':
            {
                if (line[2] == '-') configData.config.measurementSend = false;
                else if (line[2] == '+') configData.config.measurementSend = true;
                break;
            }
/**
<li> <b>r-, r+</b> Turn recording on or off */
        case 'r':
            {
                if (line[2] == '-') configData.config.recording = false;
                else if ((line[2] == '+') && (writeFileHandle < 0x0FF))
                    configData.config.recording = true;
                break;
            }
/*--------------------*/
/* BATTERY parameters */
/**
<li> <b>Tntxx</b> Set battery type and capacity, n is battery, t is type,
xx is capacity */
        case 'T':
            {
                if (battery < 3)
                {
                    uint8_t type = line[3]-'0';
                    if (type < 3)
                    {
                        configData.config.batteryType[battery] =
                            (battery_Type)type;
                        configData.config.batteryCapacity[battery] =
                            asciiToInt((char*)line+4);
                        setBatteryChargeParameters(battery);
                    }
                }
                break;
            }
/**
<li> <b>m-, m+</b> Turn on/off battery missing */
        case 'm':
            {
                if (line[3] == '-') setBatteryMissing(battery,false);
                else if (line[3] == '+') setBatteryMissing(battery,true);
                break;
            }
/**
<li> <b>Inxx</b> Set bulk current limit, n is battery, xx is limit */
        case 'I':
            {
                if (battery < 3)
                    configData.config.bulkCurrentLimitScale[battery] =
                        asciiToInt((char*)line+3);
                break;
            }
/**
<li> <b>Anxx</b> Set battery gassing voltage limit, n is battery, xx is limit */
        case 'A':
            {
                if (battery < 3)
                    configData.config.absorptionVoltage[battery] =
                        asciiToInt((char*)line+3);
                break;
            }
/**
<li> <b>fnxx</b> Set battery float current trigger, n is battery, xx is trigger */
        case 'f':
            {
                if (battery < 3)
                    configData.config.floatStageCurrentScale[battery] =
                        asciiToInt((char*)line+3);
                break;
            }
/**
<li> <b>Fnxx</b> Set battery float voltage limit, n is battery, xx is limit */
        case 'F':
            {
                if (battery < 3)
                    configData.config.floatVoltage[battery] =
                        asciiToInt((char*)line+3);
                break;
            }
/**
<li> <b>zn</b> zero current calibration by forcing current offset, n is battery */
        case 'z':
            {
                if (battery < 3)
                    setCurrentOffset(battery,getCurrent(battery));
                break;
            }
/*--------------------*/
/* MONITOR parameters */
/**
<li> <b>sm</b> Set monitor strategy byte m for keeping isolation or avoiding
loading the battery under charge. */
        case 's':
            {
                uint8_t monitorStrategy = line[2]-'0';
                if (monitorStrategy <= 3)
                    configData.config.monitorStrategy = monitorStrategy;
                break;
            }
/**
<li> <b>vx</b> set low voltage threshold, x is voltage times 256. */
        case 'v':
            {
                configData.config.lowVoltage = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Vx</b> set critical voltage threshold, x is voltage times 256. */
        case 'V':
            {
                configData.config.criticalVoltage = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>xx</b> set low SoC threshold, x is voltage times 256. */
        case 'x':
            {
                configData.config.lowSoC = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Xx</b> set critical SoC threshold, x is voltage times 256. */
        case 'X':
            {
                configData.config.criticalSoC = asciiToInt((char*)line+2);
                break;
            }
/*--------------------*/
/* CHARGER parameters */
/**
<li> <b>Sm</b> set charger strategy byte m. */
        case 'S':
            {
                uint8_t chargerStrategy = line[2]-'0';
                if (chargerStrategy < 2)
                    configData.config.chargerStrategy = chargerStrategy;
                break;
            }
/**
<li> <b>Rx</b> set charger algorithm minimum rest time x in seconds. */
        case 'R':
            {
                configData.config.restTime = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Ax</b> set charger algorithm minimum gassing phase time x in seconds. */
        case 'G':
            {
                configData.config.absorptionTime = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Dx</b> set charger minimum duty cycle x in seconds. */
        case 'D':
            {
                configData.config.minDutyCycle = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Fx</b> set charger time to float x in seconds. */
        case 'e':
            {
                configData.config.floatTime = asciiToInt((char*)line+2);
                break;
            }
/**
<li> <b>Bx</b> set charger SoC x to change from float to bulk phase. */
        case 'B':
            {
                configData.config.floatBulkSoC = asciiToInt((char*)line+2);
                break;
            }
        }
    }
/**
</ul> */

/* ======================== File commands ================ */
/*
F           - get free clusters
Wfilename   - Open file for read/write. Filename is 8.3 string style. Returns handle.
Rfilename   - Open file read only. Filename is 8.3 string style. Returns handle.
Xfilename   - Delete the file. Filename is 8.3 string style.
Cxx         - Close file. x is the file handle.
Gxx         - Read a record from read or write file.
Ddirname    - Get a directory listing. Directory name is 8.3 string style.
d[dirname]  - Get the first (if dirname present) or next entry in directory.
s           - Get status of open files and configData.config.recording flag
M           - Mount the SD card.
All commands return an error status byte at the end.
Only one file for writing and a second for reading is possible.
Data is not written to the file externally. */
/**
<b>File Commands</b> */
    else if (line[0] == 'f')
    {
        switch (line[1])
        {
/**
<ul>
<li> <b>F</b> Return number of free clusters followed by the cluster size in bytes. */
            case 'F':
            {
                uint8_t wordBuf;
                uint32_t freeClusters = 0;
                uint32_t sectorCluster = 0;
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    sendFileCommand('F',0,line+2);
                    uint8_t i;
                    for (i=0; i<4; i++)
                    {
                        wordBuf = 0;
                        xQueueReceive(fileReceiveQueue,&wordBuf,portMAX_DELAY);
                        freeClusters |= (wordBuf << 8*i);
                    }
                    for (i=0; i<4; i++)
                    {
                        wordBuf = 0;
                        xQueueReceive(fileReceiveQueue,&wordBuf,portMAX_DELAY);
                        sectorCluster |= (wordBuf << 8*i);
                    }
                    dataMessageSend("fF",freeClusters,sectorCluster);
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    xSemaphoreGive(fileSendSemaphore);
                }
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>Wf</b> Open a file f=filename for writing */
            case 'W':
            {
                if (stringLength((char*)line+2) < 12)
                {
                    uint8_t fileStatus = FR_INT_ERR;
                    if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                    {
                        stringCopy(writeFileName,(char*)line+2);
                        sendFileCommand('W',13,line+2);
                        xQueueReceive(fileReceiveQueue,&writeFileHandle,portMAX_DELAY);
                        sendResponse("fW",writeFileHandle);
                        xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                        xSemaphoreGive(fileSendSemaphore);
                    }
                    sendResponse("fE",(uint8_t)fileStatus);
                }
                break;
            }
/**
<li> <b>Rf</b> Open a file f=filename for Reading */
            case 'R':
            {
                if (stringLength((char*)line+2) < 12)
                {
                    uint8_t fileStatus = FR_INT_ERR;
                    if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                    {
                        stringCopy(readFileName,(char*)line+2);
                        sendFileCommand('R',13,line+2);
                        xQueueReceive(fileReceiveQueue,&readFileHandle,portMAX_DELAY);
                        sendResponse("fR",readFileHandle);
                        xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                        xSemaphoreGive(fileSendSemaphore);
                    }
                    sendResponse("fE",(uint8_t)fileStatus);
                }
                break;
            }
/**
<li> <b>Ghh</b> hh is the file handle. Close file for write or read file. The
file handle is a two character integer. */
            case 'C':
            {
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    uint8_t fileHandle = asciiToInt((char*)line+2);
                    sendFileCommand('C',1,&fileHandle);
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    if (fileStatus == FR_OK)
                    {
                        if (writeFileHandle == fileHandle) writeFileHandle = 0xFF;
                        else if (readFileHandle == fileHandle) readFileHandle = 0xFF;
                    }
                    xSemaphoreGive(fileSendSemaphore);
                }
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>Ghh</b> hh is the file handle. Read a record of data from the read or
write file. The data starts from the end of the previous block that was read
(or the file start if just opened). The file handle is a two character integer.
A block of bytes is read from the file and stored in a circular buffer. A record
is taken from this block and sent, the rest remains in the buffer until the next
request. */
#define GET_RECORD_SIZE 80
            case 'G':
            {
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    int numberRecords = asciiToInt((char*)line+2);
                    if (numberRecords < 1) numberRecords = 1;
                    static FRESULT fileStatus = FR_OK;
                    static uint8_t buffer[GET_RECORD_SIZE];
                    static uint8_t readPointer = 0;
                    static uint8_t writePointer = 0;
                    char sendData[GET_RECORD_SIZE];
                    uint8_t sendPointer = 0;
                    uint8_t fileHandle = asciiToInt((char*)line+2);
                    uint8_t blockLength = GET_RECORD_SIZE-1;
                    uint8_t numRead;
                    uint8_t parameters[2] = {fileHandle, blockLength};
                    while (numberRecords > 0)
                    {
/* The buffer is empty, so fill up. */
                        if (readPointer == writePointer)
                        {
                            sendFileCommand('G',2,parameters);
                            numRead = 0;
                            xQueueReceive(fileReceiveQueue,&numRead,portMAX_DELAY);
/* As records are written in entirety, premature EOF should not happen. */
                            if (numRead != blockLength)
                            {
                                fileStatus = FR_DENIED;
                                break;
                            }
                            uint8_t i;
/* Read the entire block to the local buffer. */
                            for (i=0; i<numRead; i++)
                            {
                                uint8_t nextWritePointer = (writePointer+1)
                                                % GET_RECORD_SIZE;
                                xQueueReceive(fileReceiveQueue,
                                    buffer+writePointer,portMAX_DELAY);
                                writePointer = nextWritePointer;
                            }
/* Get status byte. */
                            xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                        }
/* Assemble the data message until EOL encountered, or block exhausted. */
                        while (sendPointer < GET_RECORD_SIZE-1)
                        {
                            sendData[sendPointer] = buffer[readPointer];
                            readPointer = (readPointer+1) % GET_RECORD_SIZE;
                            if (sendData[sendPointer] == '\n')
                            {
                                sendData[sendPointer+1] = 0;
                                sendString("fG",sendData);
                                sendPointer = 0;
                                numberRecords--;
                                break;
                            }
/* If the current block is exhausted, go get some more. */
                            if (readPointer == writePointer) break;
                            sendPointer++;
                        }
                    }
                    xSemaphoreGive(fileSendSemaphore);
                }
/* Status sent is from the last time the file was read. */
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>Dd</b> Get a directory listing d=dirname. Directory name is 8.3 string
style. Gets all items in the directory and sends the type,size and name, each
group preceded by a comma. The file command requests each entry in turn,
terminated by a null filename when the directory listing is exhausted. */
            case 'D':
            {
                if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_TIMEOUT))
                    break;
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    char firstCharacter;
                    sendFileCommand('D',13,line+2);
                    commsPrintString("fD");
                    do
                    {
                        char type = 0;
/* Single character entry type */
                        xQueueReceive(fileReceiveQueue,&type,portMAX_DELAY);
                        char character;
/* Four bytes of file size */
                        uint32_t fileSize = 0;
                        uint8_t i;
                        for (i=0; i<4; i++)
                        {
                            character = 0;
                            xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                            fileSize = (fileSize << 8) + character;
                        }
/* Filename. If the first character of name is zero then the listing is ended */
                        character = 0;
                        xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                        firstCharacter = character;
                        if (firstCharacter > 0)
                        {
                            commsPrintString(",");
                            commsPrintChar(&type);
                            commsPrintHex(fileSize >> 16);
                            commsPrintHex(fileSize & 0xFFFF);
                            while (character > 0)
                            {
                                commsPrintChar(&character);
                                character = 0;
                                xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                            }
/* End of directory entry. Discard the status byte */
                            xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                            uint8_t eol = 0;
/* Send a zero parameter to ask for the next entry */
                            sendFileCommand('D',1,&eol);
                        }
                    }
                    while (firstCharacter > 0);
                    commsPrintString("\r\n");
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    xSemaphoreGive(fileSendSemaphore);
                }
                xSemaphoreGive(commsSendSemaphore);
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>d[d]</b> d is the d=directory name. Get the first (if d present) or next
entry in the directory. If the name has a zero in the first position, return the
next entry in the directory listing. Returns the type, size and name preceded by
a comma for compatibility with the full directory listing request. If there are
no further entries found in the directory, then an empty string is sent back. */
            case 'd':
            {
                if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_TIMEOUT))
                    break;
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    sendFileCommand('D',13,line+2);
                    commsPrintString("fd");
                    char type = 0;
/* Single character entry type */
                    xQueueReceive(fileReceiveQueue,&type,portMAX_DELAY);
                    char character;
/* Four bytes of file size */
                    uint32_t fileSize = 0;
                    uint8_t i;
                    for (i=0; i<4; i++)
                    {
                        character = 0;
                        xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                        fileSize = (fileSize << 8) + character;
                    }
/* Filename. If the first character of name is zero then the listing is ended */
                    character = 0;
                    xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                    if (character > 0)  /* Pull in remaining characters of name */
                    {
                        commsPrintString(",");
                        commsPrintChar(&type);
                        commsPrintHex(fileSize >> 16);
                        commsPrintHex(fileSize & 0xFFFF);
                        while (character > 0)
                        {
                            commsPrintChar(&character);
                            character = 0;
                            xQueueReceive(fileReceiveQueue,&character,portMAX_DELAY);
                        }
                    }
                    commsPrintString("\r\n");
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    xSemaphoreGive(fileSendSemaphore);
                }
                xSemaphoreGive(commsSendSemaphore);
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>M</b> Register (mount or remount) the SD card. */
            case 'M':
            {
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    sendFileCommand('M',0,line+2);
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    xSemaphoreGive(fileSendSemaphore);
                }
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
<li> <b>s</b> Send a status message containing: software switches
(configData.config.recording), names of open files. */
            case 's':
            {
                if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_TIMEOUT))
                    break;;
                commsPrintString("fs,");
                commsPrintInt((int)getControls());
                commsPrintString(",");
                uint8_t writeStatus;
                commsPrintInt(writeFileHandle);
                commsPrintString(",");
                if (writeFileHandle < 0xFF)
                {
                    commsPrintString(writeFileName);
                    commsPrintString(",");
                }
                commsPrintInt(readFileHandle);
                if (readFileHandle < 0xFF)
                {
                    commsPrintString(",");
                    commsPrintString(readFileName);
                }
                commsPrintString("\r\n");
                xSemaphoreGive(commsSendSemaphore);
                break;
            }
/**
<li> <b>Xf</b> Delete a designated file f=filename. The file must not be open at
the time. A status is returned to signal a directory refresh. */
            case 'X':
            {
                uint8_t fileStatus = FR_INT_ERR;
                if (xSemaphoreTake(fileSendSemaphore,COMMS_FILE_TIMEOUT))
                {
                    sendFileCommand('X',13,line+2);
                    xQueueReceive(fileReceiveQueue,&fileStatus,portMAX_DELAY);
                    xSemaphoreGive(fileSendSemaphore);
                }
                sendResponse("fE",(uint8_t)fileStatus);
                break;
            }
/**
</ul> */
        }
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Reset Timer Callback

This is called when the timer on the overcurrent reset line expires. It releases
all reset lines. The assumption is that a reset called externally will be released
before a new call to reset is issued.

@note: the timer handle passed through the FreeRTSO timer call did not work so a
global is used until this is fixed.
*/

void resetCallback(xTimerHandle resetHandle)
{
    resetHandle = resetHandle;
    overCurrentRelease(intf);
}

/*--------------------------------------------------------------------------*/
/** @brief Send a data message with two parameters

This is an asynchronous TxPDO in CANopen and therefore is nominally of fixed
length. The response parameters are converted to ASCII integer for debug.

This will abandon the message if commsSendSemaphore cannot be obtained.

@param ident: char* an identifier string recognized by the receiving program.
@param param1: int32_t first integer parameter.
@param param2: int32_t second integer parameter.
*/

void dataMessageSend(char* ident, int32_t param1, int32_t param2)
{
    if (configData.config.measurementSend)
    {
        if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
        commsPrintString(ident);
        commsPrintString(",");
        commsPrintInt(param1);
        commsPrintString(",");
        commsPrintInt(param2);
        commsPrintString("\r\n");
        xSemaphoreGive(commsSendSemaphore);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a data message with two parameters at low priority.

This is the same as dataMessageSend except that a message is only sent if the
commsSendQueue is empty.

This blocks indefinitely until the queue is empty of all messages.

@param ident: char* an identifier string recognized by the receiving program.
@param param1: int32_t first integer parameter.
@param param2: int32_t second integer parameter.
*/

void dataMessageSendLowPriority(char* ident, int32_t param1, int32_t param2)
{
    if (configData.config.measurementSend)
    {
/**
If any characters are on the send queue, block on the commsEmptySemaphore
which is released by the ISR after the last character has been sent. One
message is then sent. The calling task cannot queue more than one message.
Block indefinitely as the message must not be abandoned */
        while (uxQueueMessagesWaiting(commsSendQueue) > 0)
            xSemaphoreTake(commsEmptySemaphore,portMAX_DELAY);
        if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
        commsPrintString(ident);
        commsPrintString(",");
        commsPrintInt(param1);
        commsPrintString(",");
        commsPrintInt(param2);
        commsPrintString("\r\n");
        xSemaphoreGive(commsSendSemaphore);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a data message with one parameter

Use to send a simple response to a command. The calling task will block until
commsSendSemaphore is available.

@param[in] ident: char* Response identifier string
@param[in] parameter: int32_t Single integer parameter.
*/

void sendResponse(char* ident, int32_t parameter)
{
    if (configData.config.measurementSend)
    {
        if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
        commsPrintString(ident);
        commsPrintString(",");
        commsPrintInt(parameter);
        commsPrintString("\r\n");
        xSemaphoreGive(commsSendSemaphore);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a data message with one parameter at low priority.

Use to send a simple response to a command. The calling task will block until
commsSendSemaphore is available.

@param[in] ident: char* Response identifier string
@param[in] parameter: int32_t Single integer parameter.
*/

void sendResponseLowPriority(char* ident, int32_t parameter)
{
    if (configData.config.measurementSend)
    {
/**
If any characters are on the send queue, block on the commsEmptySemaphore
which is released by the ISR after the last character has been sent. One
message is then sent. The calling task cannot queue more than one message.
Block indefinitely as the message must not be abandoned */
        while (uxQueueMessagesWaiting(commsSendQueue) > 0)
            xSemaphoreTake(commsEmptySemaphore,portMAX_DELAY);
        if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
        commsPrintString(ident);
        commsPrintString(",");
        commsPrintInt(parameter);
        commsPrintString("\r\n");
        xSemaphoreGive(commsSendSemaphore);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a debug message with one parameter at low priority.

Use to send a simple debug response to a command. The calling task will block
until commsSendSemaphore is available.

@param[in] ident: char* Response identifier string
@param[in] parameter: int32_t Single integer parameter.
*/

void sendDebugResponse(char* ident, int32_t parameter)
{
    if ((ident[0] == 'D') && !configData.config.debugMessageSend) return;
/**
If any characters are on the send queue, block on the commsEmptySemaphore
which is released by the ISR after the last character has been sent. One
message is then sent. The calling task cannot queue more than one message.
Block indefinitely as the message must not be abandoned */
    while (uxQueueMessagesWaiting(commsSendQueue) > 0)
        xSemaphoreTake(commsEmptySemaphore,portMAX_DELAY);
    if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
    commsPrintString(ident);
    commsPrintString(",");
    commsPrintInt(parameter);
    commsPrintString("\r\n");
    xSemaphoreGive(commsSendSemaphore);
}

/*--------------------------------------------------------------------------*/
/** @brief Send a string

Use to send a string. This simply abandons the message if it cannot get the
commsSendSemaphore.

@param[in] ident: char* Response identifier string
@param[in] string: char* Single integer parameter.
*/

void sendString(char* ident, char* string)
{
    if (configData.config.measurementSend)
    {
        if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >=
            stringLength(ident)+stringLength(string)+3)
        {
            if (! xSemaphoreTake(commsSendSemaphore,COMMS_SEND_DELAY)) return;
            commsPrintString(ident);
            commsPrintString(",");
            commsPrintString(string);
            commsPrintString("\r\n");
            xSemaphoreGive(commsSendSemaphore);
        }
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a string at low priority.

Use to send a string. A message is only sent if the commsSendQueue is empty.

This blocks indefinitely until the queue is empty of all messages.

@param[in] ident: char* Response identifier string
@param[in] string: char* Arbitrary length string.
*/

void sendStringLowPriority(char* ident, char* string)
{
    if (configData.config.measurementSend)
    {
/**
If any characters are on the send queue, block on the commsEmptySemaphore
which is released by the ISR after the last character has been sent. One
message is then sent. The calling task cannot queue more than one message.
Block indefinitely as the message must not be abandoned */
        if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >=
            stringLength(ident)+stringLength(string)+3)
        {
            while (uxQueueMessagesWaiting(commsSendQueue) > 0)
                xSemaphoreTake(commsEmptySemaphore,portMAX_DELAY);
            xSemaphoreTake(commsSendSemaphore,portMAX_DELAY);
            commsPrintString(ident);
            commsPrintString(",");
            commsPrintString(string);
            commsPrintString("\r\n");
            xSemaphoreGive(commsSendSemaphore);
        }
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Send a debug string at low priority.

Use to send a debug string. A message is only sent if the commsSendQueue is
empty and the debug messages are enabled.

This blocks indefinitely until the queue is empty of all messages.

@param[in] ident: char* Response identifier string
@param[in] string: char* Single integer parameter string.
*/

void sendDebugString(char* ident, char* string)
{
    if ((ident[0] == 'D') && !configData.config.debugMessageSend) return;
/**
If any characters are on the send queue, block on the commsEmptySemaphore
which is released by the ISR after the last character has been sent. One
message is then sent. The calling task cannot queue more than one message.
Block indefinitely as the message must not be abandoned */
    if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >=
        stringLength(ident)+stringLength(string)+3)
    {
        while (uxQueueMessagesWaiting(commsSendQueue) > 0)
            xSemaphoreTake(commsEmptySemaphore,portMAX_DELAY);
        xSemaphoreTake(commsSendSemaphore,portMAX_DELAY);
        commsPrintString(ident);
        commsPrintString(",");
        commsPrintString(string);
        commsPrintString("\r\n");
        xSemaphoreGive(commsSendSemaphore);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Print out the contents of a register (debug)

@param[in] reg: uint32_t full address of register.

*/

void commsPrintRegister(uint32_t reg)
{
    if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >= 11)
    {
        commsPrintHex((reg >> 16) & 0xFFFF);
        commsPrintHex((reg >> 00) & 0xFFFF);
        commsPrintChar(" ");
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Print out a value in ASCII decimal form (ack Thomas Otto)

@param[in] value: int32_t integer value to be printed.
*/

void commsPrintInt(int32_t value)
{
    uint8_t i=0;
    char buffer[25];
    intToAscii(value, buffer);
    if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >= stringLength(buffer))
        while (buffer[i] > 0)
        {
            commsPrintChar(&buffer[i]);
            i++;
        }
}

/*--------------------------------------------------------------------------*/
/** @brief Print out a value in 16 bit ASCII hex form

@param[in] value: int32_t integer value to be printed.
*/

void commsPrintHex(uint32_t value)
{
    uint8_t i;
    char buffer[25];

    for (i = 0; i < 4; i++)
    {
        buffer[i] = "0123456789ABCDEF"[value & 0xF];
        value >>= 4;
    }
    if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >= 5)
    {
        for (i = 4; i > 0; i--)
        {
            commsPrintChar(&buffer[i-1]);
        }
/*    commsPrintChar(" "); */
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Print a String

@param[in] ch: char* pointer to string to be printed.
*/

void commsPrintString(char *ch)
{
    if ((uint16_t)uxQueueSpacesAvailable(commsSendQueue) >= stringLength(ch))
        while(*ch) commsPrintChar(ch++);
}

/*--------------------------------------------------------------------------*/
/** @brief Print a Character

Disable the Tx interrupt first to prevent queue corruption in the ISR.
This is where the characters are queued for the ISR to transmit.
The ISR is in the hardware module.

Characters are placed on a queue and picked up by the ISR for transmission.
The application is responsible for protecting a message with semaphores
to ensure it is sent in entirety (see convenience functions defined here).

If the queue fails to respond it is reset. A number of messages will be lost but
hopefully the application will continue to run. A receiving program may see
a corrupted message.

@param[in] ch: char* pointer to character to be printed.
*/

void commsPrintChar(char *ch)
{
    if (configData.config.enableSend)
    {
        commsEnableTxInterrupt(false);
        while (xQueueSendToBack(commsSendQueue,ch,COMMS_SEND_TIMEOUT) != pdTRUE)
        {
            xQueueReset(commsSendQueue);
            commsEnableTxInterrupt(true);
            taskYIELD();
        }
        commsEnableTxInterrupt(true);
    }
}

/*--------------------------------------------------------------------------*/
/** @brief Callback function to lapse communications

This is called if the lapse communications timer is not reset by a received
message within the timeout period. The GUI is expected to regularly send a
message of some sort to keep communications alive. This feature allows
power hungry communications circuits to be used only when the GUI is open.

@param lapseCommsTimer: xTimerHandle handle for Communications Timer Function
*/

void lapseCommsCallback(xTimerHandle lapseCommsTimer)
{
    lapseCommsTimer = lapseCommsTimer;
    configData.config.enableSend = false;    
}

/**@}*/

