QT          =
INCLUDEPATH = ../../src
SOURCES     = ../../src/main.cpp ../../src/wsserver.cpp ../../src/wsregex.cpp worker.cpp
HEADERS     =                    ../../src/wsserver.h   ../../src/wsregex.h   worker.h   router.h
LIBS        = -lfcgi++ -lfcgi -lsqlite3 -ljson-c
