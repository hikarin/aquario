#  Makefile for System-V flavoured UNIX
#
CFLAGS = -Wall
CC = gcc
CSOURCES = aquario.c gc_base.c gc_copy.c gc_markcompact.c gc_reference_count.c gc_generational.c

TARGET = aquario
SOURCES = $(CSOURCES)

.SUFFIXES: .o .d .c

all: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS)

clean:
	rm -f core *.o $(TARGET)

test: $(CSOURCES)
	$(CC) $(CSOURCES) -o $(TARGET) $(CFLAGS) -D_TEST
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

