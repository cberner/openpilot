TEMPLATE = lib
TARGET = HITLNEW
QT += network
include(../../openpilotgcsplugin.pri)
include(hitlnew_dependencies.pri)
HEADERS += hitlplugin.h \
    hitlwidget.h \
    hitloptionspage.h \
    hitlfactory.h \
    hitlconfiguration.h \
    hitlgadget.h \
    simulator.h \
    fgsimulator.h \
    il2simulator.h
SOURCES += hitlplugin.cpp \
    hitlwidget.cpp \
    hitloptionspage.cpp \
    hitlfactory.cpp \
    hitlconfiguration.cpp \
    hitlgadget.cpp \
    simulator.cpp \
    il2simulator.cpp \
    fgsimulator.cpp
OTHER_FILES += HITLNEW.pluginspec
FORMS += hitloptionspage.ui \
    hitlwidget.ui
RESOURCES += hitlresources.qrc