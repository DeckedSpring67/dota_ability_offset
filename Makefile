CC=gcc
#CFLAGS=-g -Ofast -march=native -mavx2
CFLAGS=-g -march=native -mavx2
LDFLAGS=-lX11 -lpng -lpthread

level_offset: main.o
	$(CC) $(LDFLAGS) -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -f *.o main
