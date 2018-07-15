#  Makefile for System-V flavoured UNIX
#
CFLAGS = -Wall -D_MEASURE
CC = gcc
CSOURCES = aquario.c gc/base.c gc/copy.c gc/markcompact.c gc/reference_count.c gc/generational.c gc/marksweep.c
LIBS = -lpthread

TARGET = aquario
SOURCES = $(CSOURCES)

.SUFFIXES: .o .d .c

all: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f core *.o $(TARGET)

tests: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_TEST $(LIBS)
	@test/test.sh ms
	@test/test.sh mc
	@test/test.sh copy
	@test/test.sh ref
	@test/test.sh gen

debug: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_DEBUG -g $(LIBS)

opt: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -O2 $(LIBS)

prof: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -pg $(LIBS)

