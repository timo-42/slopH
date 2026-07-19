CC ?= cc
AR ?= ar
ARFLAGS = rcs
CPPFLAGS += -Iinclude -Ivendor/yyjson -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2
STRICT_CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic

BUILD_DIR ?= build
OBJECT_DIR := $(BUILD_DIR)/obj
TEST_DIR := $(BUILD_DIR)/tests
BINARY_DIR := $(BUILD_DIR)/bin
LIBRARY := $(BUILD_DIR)/libsloph-bootstrap.a
COMPILER := $(BINARY_DIR)/sloph

LIBRARY_SOURCES := $(filter-out src/cli.c,$(wildcard src/*.c)) vendor/yyjson/yyjson.c
LIBRARY_OBJECTS := $(patsubst %.c,$(OBJECT_DIR)/%.o,$(LIBRARY_SOURCES))
TEST_SOURCES := $(wildcard tests/test_*.c)
TEST_BINARIES := $(patsubst tests/%.c,$(TEST_DIR)/%,$(TEST_SOURCES))
DEPENDENCIES := $(LIBRARY_OBJECTS:.o=.d)

.PHONY: all clean core-cases test sanitize

all: $(LIBRARY) $(COMPILER)

$(LIBRARY): $(LIBRARY_OBJECTS)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(COMPILER): src/cli.c $(LIBRARY)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(STRICT_CFLAGS) $(CFLAGS) $< $(LIBRARY) $(LDFLAGS) $(LDLIBS) -o $@

$(OBJECT_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(STRICT_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_DIR)/%: tests/%.c $(LIBRARY)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(STRICT_CFLAGS) $(CFLAGS) $< $(LIBRARY) $(LDFLAGS) $(LDLIBS) -o $@

test: all $(TEST_BINARIES)
	@set -e; for test_binary in $(TEST_BINARIES); do $$test_binary; done

core-cases: all
	SLOPH_C_COMPILER="$(abspath $(COMPILER))" \
		../../tests/implementation/c/run-core-cases.sh

sanitize:
	$(MAKE) BUILD_DIR=build/sanitize \
		CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" test

clean:
	rm -rf build

-include $(DEPENDENCIES)
