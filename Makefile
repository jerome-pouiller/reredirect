override CFLAGS+=-Wall -g
OBJS=reredirect.o ptrace.o attach.o

# Note that because of how Make works, this can be overriden from the
# command-line.
#
# e.g. install to /usr with `make PREFIX=/usr`
PREFIX=/usr/local

all: reredirect

reredirect: $(OBJS)

attach.o: reredirect.h ptrace.h
reredirect.o: reredirect.h
ptrace.o: ptrace.h $(wildcard arch/*.h)

clean:
	rm -f reredirect $(OBJS)

install: reredirect relink
	install -d -m 755 $(DESTDIR)$(PREFIX)/bin/
	install -m 755 reredirect $(DESTDIR)$(PREFIX)/bin/reredirect
	install -m 755 relink $(DESTDIR)$(PREFIX)/bin/relink
	install -d -m 755 $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 reredirect.1 $(DESTDIR)$(PREFIX)/share/man/man1/reredirect.1
