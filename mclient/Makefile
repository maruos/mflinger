CC=gcc
CFLAGS=-Wall
LIBS=-lX11 -lXfixes -lXext -lXdamage -lXi -lXrandr -lpthread libmaru.a

mclient: mclient.c libmaru.a mcursor_cache.o
	$(CC) $(CFLAGS) mcursor_cache.o -o mclient mclient.c $(LIBS)

libmaru.a: mlib.o
	ar rcs libmaru.a mlib.o

mlib: mlib.c
	$(CC) $(CFLAGS) -c mlib.c

mcursor_cache: mcursor_cache.c
	$(CC) $(CFLAGS) -c mcursor_cache.c

clean:
	-rm mclient *.o
