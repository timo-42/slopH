CC ?= cc
CFLAGS ?= -O2
CPPFLAGS += -I../../src/sloph-c-bootstrap/vendor/yyjson
STRICT_CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic

.PHONY: all test clean

all: build/sloph-jsonfmt

build/sloph-jsonfmt: main.c ../../src/sloph-c-bootstrap/vendor/yyjson/yyjson.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(STRICT_CFLAGS) $(CFLAGS) $^ -o $@

test: all
	./test.sh ./build/sloph-jsonfmt

clean:
	rm -rf build
