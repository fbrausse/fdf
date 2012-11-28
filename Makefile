
CFLAGS = -Wall -Wextra -Wno-unused -fomit-frame-pointer -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -std=c99 -D_HAVE_GETTEXT -O2
LDFLAGS = -Wl,--as-needed -Wl,-O1

all: fdf
#	strip --strip-unneeded -R .comment fdf

fdf: dl_list.o fdf.o fcmp2.o

clean:
	$(RM) fdf
	$(RM) *.o
