CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror -g

.PHONY: all clean

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c -lpthread -lrt

clean:
	rm -f aesdsocket *.o