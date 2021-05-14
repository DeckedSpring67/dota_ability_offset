CC=gcc
CFLAGS=-g
LDFLAGS=-lX11 -lpng

level_offset: main.o
	$(CC) $(LDFLAGS) -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -f *.o main
