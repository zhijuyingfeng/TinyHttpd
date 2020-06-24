TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        httpd.c\
        client.c
QMAKE_CXXFLAGS += -std=c++0x -pthread
LIBS += -pthread
