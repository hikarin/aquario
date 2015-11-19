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

test: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_TEST
	./aquario -GC ms   do_test.lsp
	./aquario -GC gen  do_test.lsp
	./aquario -GC copy do_test.lsp
	./aquario -GC mc   do_test.lsp
	./aquario -GC ref  do_test.lsp

debug: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_DEBUG -g

opt: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -O2

prof: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -pg

