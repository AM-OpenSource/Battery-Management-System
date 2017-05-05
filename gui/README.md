Battery Management System GUI
-----------------------------

The GUI provides a means of displaying performance and status information in
numerical and graphical form, setting up data recording on the remote system
and configuring the system.

It can connect via serial (directly or using a serial to USB adapter), or by
TCP/IP when connected through an intermediate machine that maps the serial
interface to a TCP/IP interface. This makes it possible to monitor the system
remotely over Internet.

Currently the constant SERIAL in the header power-management.h is defined if
the serial version is desired. Otherwise it is left undefined to use the TCP
version.

QWT must be installed and the .pro file modified if necessary to point to it.

To compile this program, ensure that QT5 is installed.

make clean
qmake
make

Call with power-management [options]

If SERIAL is defined the following apply:

-P   port (/dev/ttyUSB0 default)

-b   baudrate (from 2400, 4800, 9600, 19200, 38400 default, 57600, 115200)

Otherwise if SERIAL is not defined the following apply:

-a   TCP address (192.168.2.16 default)

-p   TCP port (6666 default)

More information is available on [Jiggerjuice](http://www.jiggerjuice.info/electronics/projects/solarbms/solarbms-gui.html).

(c) K. Sarkies 05/05/2017

TODO

1. File - add file info (date).
2. Download feature (may cause saturation of the comms interface).
3. Make into a single compile binary for serial and TCP versions.

