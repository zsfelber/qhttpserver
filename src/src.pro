include(../qhttpserver.pri)

QHTTPSERVER_BASE = ..
TEMPLATE = lib

TARGET = qhttpserver

!win32:VERSION = 0.1.0

QT += network
QT -= gui

CONFIG += dll debug_and_release c++11

CONFIG(debug, debug|release) {
    win32: TARGET = $$join(TARGET,,,d)
}

DEFINES += QHTTPSERVER_EXPORT

INCLUDEPATH += $$QHTTPSERVER_BASE/http-parser

PRIVATE_HEADERS += $$QHTTPSERVER_BASE/http-parser/http_parser.h qhttpconnection.h

PUBLIC_HEADERS += qhttpserver.h qhttprequest.h qhttpresponse.h qhttpserverapi.h qhttpserverfwd.h

HEADERS = $$PRIVATE_HEADERS $$PUBLIC_HEADERS \
    safequeue.h \
    websockets/qdefaultmaskgenerator_p.h \
    websockets/qmaskgenerator.h \
    websockets/qsslserver_p.h \
    websockets/qwebsocket.h \
    websockets/qwebsocket_p.h \
    websockets/qwebsocketcorsauthenticator.h \
    websockets/qwebsocketcorsauthenticator_p.h \
    websockets/qwebsocketdataprocessor_p.h \
    websockets/qwebsocketframe_p.h \
    websockets/qwebsockethandshakerequest_p.h \
    websockets/qwebsockethandshakeresponse_p.h \
    websockets/qwebsocketprotocol.h \
    websockets/qwebsocketprotocol_p.h \
    websockets/qwebsockets_global.h \
    websockets/qwebsocketserver.h \
    websockets/qwebsocketserver_p.h
SOURCES = *.cpp $$QHTTPSERVER_BASE/http-parser/http_parser.c \
    websockets/qdefaultmaskgenerator_p.cpp \
    websockets/qmaskgenerator.cpp \
    websockets/qsslserver.cpp \
    websockets/qwebsocket.cpp \
    websockets/qwebsocket_p.cpp \
    websockets/qwebsocketcorsauthenticator.cpp \
    websockets/qwebsocketdataprocessor.cpp \
    websockets/qwebsocketframe.cpp \
    websockets/qwebsockethandshakerequest.cpp \
    websockets/qwebsockethandshakeresponse.cpp \
    websockets/qwebsocketprotocol.cpp \
    websockets/qwebsocketserver.cpp \
    websockets/qwebsocketserver_p.cpp

OBJECTS_DIR = $$QHTTPSERVER_BASE/build
MOC_DIR = $$QHTTPSERVER_BASE/build
DESTDIR = $$QHTTPSERVER_BASE/lib

target.path = $$LIBDIR
headers.path = $$INCLUDEDIR
headers.files = $$PUBLIC_HEADERS
INSTALLS += target headers
