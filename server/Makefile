CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g

.PHONY: all clean

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket *.o