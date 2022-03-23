#----------------------------------------------------
#
#   Point Statistics Widget library
#   This project is part of CRITERIA-3D distribution
#
#
#----------------------------------------------------

QT  += widgets charts sql xml

TEMPLATE = lib
CONFIG += staticlib

CONFIG += debug_and_release

unix:{
    CONFIG(debug, debug|release) {
        TARGET = debug/pointStatisticsWidget
    } else {
        TARGET = release/pointStatisticsWidget
    }
}
macx:{
    CONFIG(debug, debug|release) {
        TARGET = debug/pointStatisticsWidget
    } else {
        TARGET = release/pointStatisticsWidget
    }
}
win32:{
    TARGET = pointStatisticsWidget
}

INCLUDEPATH += ../crit3dDate ../mathFunctions ../gis ../meteo ../utilities ../dbMeteoPoints ../dbMeteoGrid ../phenology ../climate ../commonDialogs ../interpolation


SOURCES += \
    pointStatisticsChartView.cpp \
    pointStatisticsCallout.cpp \
    pointStatisticsWidget.cpp


HEADERS += \
    pointStatisticsChartView.h \
    pointStatisticsCallout.h \
    pointStatisticsWidget.h 


