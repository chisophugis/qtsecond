QT       += core gui widgets

QMAKE_CXXFLAGS += -std=c++11

TARGET = video
TEMPLATE = app
SOURCES += main.cpp openglwindow.cpp

HEADERS  += openglwindow.h
#FORMS    +=
