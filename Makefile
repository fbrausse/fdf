
CFLAGS = -Wall -Wextra -Wno-unused -fomit-frame-pointer -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -std=c99 -D_HAVE_GETTEXT -O2
LDFLAGS = -Wl,--as-needed -Wl,-O1

MD = mkdir -p
INSTALL = /usr/bin/install

prefix ?= usr/local
bindir ?= $(prefix)/bin
datarootdir ?= $(prefix)/share
mandir ?= $(datarootdir)/man

.PHONY: all clean install

all: fdf
#	strip --strip-unneeded -R .comment fdf

fdf: dl_list.o fdf.o fcmp2.o

clean:
	$(RM) fdf
	$(RM) *.o

install: fdf fdf.1
	$(MD) $(DESTDIR)/$(bindir) && $(INSTALL) -m 0755 fdf $(DESTDIR)/$(bindir)
	$(MD) $(DESTDIR)/$(mandir)/man1 && $(INSTALL) -m 0644 fdf.1 $(DESTDIR)/$(mandir)/man1
