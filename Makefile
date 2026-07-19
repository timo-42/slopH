COMPILER_DIR := src/sloph-c-bootstrap
COMPILER := $(COMPILER_DIR)/build/bin/sloph

PREFIX ?= /usr/local
DESTDIR ?=
INSTALL ?= install

.PHONY: all test cases smoke check sanitize install clean

all:
	$(MAKE) -C $(COMPILER_DIR) all

test:
	$(MAKE) -C $(COMPILER_DIR) test

cases:
	$(MAKE) -C $(COMPILER_DIR) cases

smoke: all
	@set -eu; \
	temporary=$$(mktemp -d "$${TMPDIR:-/tmp}/sloph-smoke.XXXXXX"); \
	trap 'rm -rf "$$temporary"' EXIT HUP INT TERM; \
	$(COMPILER) canopy-to-crown \
		tests/v1/ast/standard-transform/input.sloph -o "$$temporary/crown.json"; \
	$(COMPILER) crown-to-heartwood examples/hello-world \
		-o "$$temporary/heartwood.core"; \
	$(COMPILER) heartwood-to-timber "$$temporary/heartwood.core" \
		--symbol hello::main::main -o "$$temporary/timber.c"; \
	$(COMPILER) run examples/hello-world >"$$temporary/run.stdout"; \
	cmp examples/hello-world/expected.stdout "$$temporary/run.stdout"; \
	$(COMPILER) compile examples/hello-world -o "$$temporary/hello"; \
	"$$temporary/hello" >"$$temporary/native.stdout"; \
	cmp examples/hello-world/expected.stdout "$$temporary/native.stdout"

check:
	$(MAKE) test
	$(MAKE) cases
	$(MAKE) smoke

sanitize:
	$(MAKE) -C $(COMPILER_DIR) sanitize

install:
	@case "$(PREFIX)" in /*) ;; *) \
		echo "PREFIX must be an absolute path" >&2; exit 2 ;; esac
	$(MAKE) -B -C $(COMPILER_DIR) BUILD_DIR=build/install \
		SLOPH_LIBRARIES_ROOT="$(PREFIX)/share/sloph/libraries" all
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin"
	$(INSTALL) -m 755 "$(COMPILER_DIR)/build/install/bin/sloph" \
		"$(DESTDIR)$(PREFIX)/bin/sloph"
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/share/sloph/libraries"
	cp -R src/libraries/. "$(DESTDIR)$(PREFIX)/share/sloph/libraries/"

clean:
	$(MAKE) -C $(COMPILER_DIR) clean
