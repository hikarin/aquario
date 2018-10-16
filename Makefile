#  Makefile for System-V flavoured UNIX
#
CFLAGS = -Wall
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
	@test/test.sh ms GC_STRESS
	@test/test.sh mc
	@test/test.sh mc GC_STRESS
	@test/test.sh copy
	@test/test.sh copy GC_STRESS
	@test/test.sh ref
	@test/test.sh ref GC_STRESS
	@test/test.sh gen
	@test/test.sh gen GC_STRESS

debug: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_DEBUG -D_MEASURE -g $(LIBS)

opt: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -O3 -fomit-frame-pointer $(LIBS)

prof: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_MEASURE -pg $(LIBS)

