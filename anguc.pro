TEMPLATE = app
TARGET = anguc

QT = core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

INCLUDEPATH +=/usr/local/include

SOURCES += \
    advancedsettings.cpp \
    connectiontab.cpp \
    exposuretab.cpp \
    tabdialog.cpp \
    main.cpp \

HEADERS += \
    advancedsettings.h \
    cliutils.h \
    settempthread.h \
    imagethread.h \
    connectiontab.h \
    exposuretab.h \
    tabdialog.h

CONFIG(sim_only) {
    message("------------------------------------------------------------")
    message("Simulation mode enabled; linking against dev/mock libraries.")
    message("------------------------------------------------------------")
    INCLUDEPATH = ../andor2k/sim $$INCLUDEPATH
    unix|win32: LIBS += -L../andor2k/src/.libs \
                        -L../andor2k/sim/lib
}

unix|win32: LIBS += -lhelmosandor2k

unix|win32: LIBS += -landor

unix|win32: LIBS += -lcfitsio
