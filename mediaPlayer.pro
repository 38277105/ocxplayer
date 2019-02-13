#-------------------------------------------------
#
# Project created by QtCreator 2017-08-21T17:27:26
#
#-------------------------------------------------

QT       += widgets axserver gui uitools

TARGET = mediaPlayer
TEMPLATE = lib

DEFINES += MEDIAPLAYER_LIBRARY

SOURCES += mediaplayer.cpp \
    safetyimpl.cpp \
    main.cpp \
    process.cpp \
    display.cpp \
    commfun.cpp

HEADERS += mediaplayer.h\
    safetyimpl.h \
    process.h \
    display.h \
    commfun.h
DEF_FILE = mediaPlayer.def

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    control.ui
win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavutil
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavutil
else:unix: LIBS += -L$$PWD/../../lib/ -lavutil

#win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavcodec
#else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavcodecd
#else:unix: LIBS += -L$$PWD/../../lib/ -lavcodec

INCLUDEPATH += $$PWD/../../include
DEPENDPATH += $$PWD/../../lib

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lSDL2
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lSDL2
else:unix: LIBS += -L$$PWD/../../lib/ -lSDL2


win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavcodec
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavcodec
else:unix: LIBS += -L$$PWD/../../lib/ -lavutil

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavformat
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavformat
else:unix: LIBS += -L$$PWD/../../lib/ -lavformat

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lswresample
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lswresample
else:unix: LIBS += -L$$PWD/../../lib/ -lswresample

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lswscale
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lswscale
else:unix: LIBS += -L$$PWD/../../lib/ -lswscale

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavfilter
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavfilter
else:unix: LIBS += -L$$PWD/../../lib/ -lavfilter

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lavdevice
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lavdevice
else:unix: LIBS += -L$$PWD/../../lib/ -lavdevice

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../lib/ -lLogWrite
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../lib/ -lLogWrite
else:unix: LIBS += -L$$PWD/../../lib/ -lLogWrite


win32: CONFIG(release, debug|release)LIBS += -L$$PWD/../../../boost/boost_1_66_0/stage/lib/ -llibboost_thread-vc120-mt-x64-1_66
else:win32:CONFIG(debug, debug|release)LIBS += -L$$PWD/../../../boost/boost_1_66_0/stage/lib/ -llibboost_thread-vc120-mt-gd-x64-1_66

INCLUDEPATH += $$PWD/../../../boost/boost_1_66_0
DEPENDPATH += $$PWD/../../../boost/boost_1_66_0



RESOURCES += \
mediaRes.qrc
