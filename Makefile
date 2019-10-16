VERSION = 0.8
CFLAGS += -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DVERSION=\"$(VERSION)\"
LDLIBS += -lz

all: gzrecover

gzrecover: gzrecover.c

clean:
	rm -f gzrecover

dist:
	rm -rf gzrt-$(VERSION) gzrt-$(VERSION).tar.gz
	mkdir gzrt-$(VERSION)
	cp Makefile NEWS README gzrecover.c gzrecover.1 README.build gzrt-$(VERSION)
	LC_ALL=C git log --pretty --numstat --summary gzrt-$(VERSION) | git2cl > gzrt-$(VERSION)/ChangeLog
	GZIP="--rsyncable --no-name" tar zcf gzrt-$(VERSION).tar.gz gzrt-$(VERSION)
	gpg --detach-sign --armor gzrt-$(VERSION).tar.gz
	rm -rf gzrt-$(VERSION)
