CC=gcc
CFLAGS=-g -O0 -std=gnu99
LIBS=-pthread

main: main.c
	$(CC) $(CFLAGS) $@.c $(LIBS) -o $@

clean:
	rm main