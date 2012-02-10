all: gzrecover

gzrecover: gzrecover.o
	cc -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 gzrecover.c -lz -o gzrecover

clean:
	rm gzrecover
