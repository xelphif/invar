CC     = cc
CFLAGS = -std=gnu23 -Wall -Wextra -Wpedantic -Wshadow -Werror -g
SAN    = -fsanitize=address,undefined

all: example

example: example.o invari.o
	$(CC) $(CFLAGS) $(SAN) -o $@ $^

%.o: %.c invari.h
	$(CC) $(CFLAGS) $(SAN) -c $<

clean:
	rm -f *.o example

.PHONY: all clean
