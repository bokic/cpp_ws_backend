QT          =
INCLUDEPATH = ../../src
SOURCES     = ../../src/main.cpp ../../src/wsserver.cpp ../../src/wsworker.cpp ../../src/wsregex.cpp ../../src/wsthreadpool.cpp
HEADERS     =                    ../../src/wsserver.h   ../../src/wsworker.h   ../../src/wsregex.h   ../../src/wsthreadpool.h   ../../src/wsroute.h
LIBS        = -lfcgi++ -lfcgi -lsqlite3 -ljson-c
