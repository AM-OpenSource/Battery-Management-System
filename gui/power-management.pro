PROJECT =       Power Management GUI
TEMPLATE =      app
TARGET          += 
DEPENDPATH      += .
QT              += widgets
QT              += serialport

QWT_ROOT        = /usr/local/qwt-6.1.3
include( $${QWT_ROOT}/features/qwt.prf )

QMAKE_RPATHDIR  *= $${QWT_ROOT}/lib

OBJECTS_DIR     = obj
MOC_DIR         = moc
UI_HEADERS_DIR  = ui
UI_SOURCES_DIR  = ui
LANGUAGE        = C++
CONFIG          += qt warn_on release
QT              += network

RESOURCES       = power-management-gui.qrc
# Input
FORMS           += power-management-main.ui
FORMS           += power-management-monitor.ui
FORMS           += power-management-configure.ui
FORMS           += power-management-record.ui
HEADERS         += power-management-main.h
HEADERS         += power-management-monitor.h
HEADERS         += power-management-configure.h
HEADERS         += power-management-record.h
SOURCES         += power-management.cpp
SOURCES         += power-management-main.cpp
SOURCES         += power-management-monitor.cpp
SOURCES         += power-management-configure.cpp
SOURCES         += power-management-record.cpp

