all:
	gcc -g -w -c Cache.c -o Cache.o 
	gcc -g -w -Wall `pkg-config fuse --cflags --libs` lfs_fuse.c Cache.o -o lfs_fuse
clean:
	rm lfs_fuse Cache.o

