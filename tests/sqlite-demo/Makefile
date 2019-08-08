DEFINES  =
CFLAGS   = -O2 -pipe -fstack-protector-strong -fno-plt -Wall -W -D_REENTRANT -fPIC $(DEFINES)
CXXFLAGS = -O2 -pipe -fstack-protector-strong -fno-plt -Wall -W -D_REENTRANT -fPIC $(DEFINES)
INCPATH  = -I. -I../../src
LFLAGS   = -Wl,-O1 -Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now
LIBS     = $(SUBLIBS) -lfcgi++ -lfcgi -lsqlite3 -ljson-c -lpthread
TARGET   = ../../bin/sqlite-demo
DEL_FILE = rm -f
OBJECTS  = ../../obj/sqlite-demo/main.o \
           ../../obj/sqlite-demo/wsserver.o \
           ../../obj/sqlite-demo/worker.o

default: ../../bin/sqlite-demo

all: ../../bin/sqlite-demo run

main.o: ../../src/main.cpp ../../obj/sqlite-demo
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o ../../obj/sqlite-demo/main.o ../../src/main.cpp

wsserver.o: ../../src/wsserver.cpp ../../obj/sqlite-demo
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o ../../obj/sqlite-demo/wsserver.o ../../src/wsserver.cpp

worker.o: worker.cpp ../../obj/sqlite-demo
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o ../../obj/sqlite-demo/worker.o worker.cpp

../../bin:
	mkdir -p ../../bin

../../obj/sqlite-demo:
	mkdir -p ../../obj/sqlite-demo

../../bin/sqlite-demo: main.o wsserver.o worker.o ../../bin
	$(CXX) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(OBJCOMP) $(LIBS)

run: ../../bin/sqlite-demo
	$(TARGET)

clean:
	-$(DEL_FILE) $(OBJECTS)

distclean: clean
	-$(DEL_FILE) $(TARGET)
