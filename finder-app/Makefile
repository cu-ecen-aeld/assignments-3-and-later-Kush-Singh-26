CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Werror

.PHONY: all clean

all: writer

writer: writer.c
	$(CC) $(CFLAGS) -o writer writer.c

clean:
	rm -f writer *.o
