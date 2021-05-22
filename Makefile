CC=gcc
CFLAGS=-g -march=native -mavx2 -Ofast
LDFLAGS=-lX11 -lpthread

level_offset: main.o
	$(CC) $(LDFLAGS) -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -f *.o main
