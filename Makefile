C_SRCS=$(wildcard src/*.c)
BINS=$(C_SRCS:src/%.c=bin/%)

all: $(BINS)

bin/%: src/%.c
	gcc -std=c11 -s -Os -Wall -Wextra -Werror -D _GNU_SOURCE -o "$@" "$<" -lutil

clean:
	rm -rf $(BINS)
