CC?=gcc
CFLAGS?=-Wall -g -O2
TARGETS=http_get_dos
#LIBS=

UNAME := $(shell uname)

all::	$(TARGETS) $(LIBS)

http_get_dos: http_get_dos.c $(LIBS)
	$(CC) -o http_get_dos http_get_dos.c $(LIBS) $(CFLAGS)

clean:
	rm -f *~ *.o $(TARGETS)
