#  Makefile for System-V flavoured UNIX
#
CFLAGS = -Wall
CC = gcc
CSOURCES = aquario.c gc/base.c gc/copy.c gc/markcompact.c gc/reference_count.c gc/generational.c gc/marksweep.c

TARGET = aquario
SOURCES = $(CSOURCES)

.SUFFIXES: .o .d .c

all: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS)

clean:
	rm -f core *.o $(TARGET)

tests: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_TEST
	@test/test.sh ms
	@test/test.sh mc
	@test/test.sh copy
	@test/test.sh ref
	@test/test.sh gen

debug: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_DEBUG -g

opt: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -O2

prof: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -pg

