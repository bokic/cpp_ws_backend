QT          =
INCLUDEPATH = ../../src
SOURCES     = ../../src/main.cpp ../../src/wsserver.cpp worker.cpp
HEADERS     =                    ../../src/wsserver.h   worker.h   router.h
LIBS        = -lfcgi++ -lfcgi -lsqlite3 -ljson-c
