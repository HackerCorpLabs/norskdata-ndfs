# norskdata-ndfs root Makefile
#
# Thin convenience wrapper around the CMake build in ndfs-c/ (the C99 libndfs
# library + the `ndtool` CLI), plus shortcuts for the TypeScript and Python
# test suites. The GitHub release workflow (.github/workflows/release.yml)
# uses the same `release` target this file exposes.
#
# Common usage:
#   make            # build ndtool (Release)
#   make release    # build ndtool, statically linked where supported
#   make test       # build + run the C unit tests
#   make clean      # remove build artifacts
#   make help       # list all targets

# ── Configuration ───────────────────────────────────────────────────
C_DIR      := ndfs-c
BUILD_DIR  := $(C_DIR)/build

# Resolve cmake to an ABSOLUTE path up front. Under WSL the full Windows PATH
# is appended (e.g. /mnt/c/Program Files/CMake/bin), and GNU make's direct
# exec of an unquoted recipe command can trip over a foreign cmake on those
# entries and fail with "cmake: Permission denied" -- even though the shell
# finds /usr/bin/cmake fine. Calling cmake by absolute path skips make's PATH
# search entirely. Override with `make CMAKE=/path/to/cmake` if needed.
CMAKE      ?= $(shell command -v cmake 2>/dev/null || echo cmake)
CTEST      ?= $(shell command -v ctest 2>/dev/null || echo ctest)
GENERATOR  ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

# Static linking is supported on Windows (MinGW) and Linux, but not macOS
# (no static libc). Detect the platform and drop the flag on Darwin.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  STATIC_FLAGS :=
else
  STATIC_FLAGS := -DCMAKE_EXE_LINKER_FLAGS=-static
endif

# ndtool binary name differs on Windows.
ifeq ($(OS),Windows_NT)
  NDTOOL := $(BUILD_DIR)/ndtool.exe
else
  NDTOOL := $(BUILD_DIR)/ndtool
endif

.DEFAULT_GOAL := all
.PHONY: all build release ndtool test test-c test-ts test-py \
        ts py install clean distclean format help

# ── Primary targets ─────────────────────────────────────────────────

## all: build ndtool (Release)
all: ndtool

## build: configure + build the whole C project (lib, tests, ndtool)
build:
	"$(CMAKE)" -S $(C_DIR) -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Release
	"$(CMAKE)" --build $(BUILD_DIR)

## ndtool: build just the ndtool CLI (Release)
ndtool:
	"$(CMAKE)" -S $(C_DIR) -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Release
	"$(CMAKE)" --build $(BUILD_DIR) --target ndtool
	@echo "Built: $(NDTOOL)"

## release: build ndtool, statically linked where the platform supports it
release:
	"$(CMAKE)" -S $(C_DIR) -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=Release $(STATIC_FLAGS)
	"$(CMAKE)" --build $(BUILD_DIR) --target ndtool
	@echo "Built (release): $(NDTOOL)"

# ── Tests ───────────────────────────────────────────────────────────

## test: build and run the C unit tests (alias for test-c)
test: test-c

## test-c: build and run the libndfs C unit tests
test-c:
	"$(CMAKE)" -S $(C_DIR) -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Release
	"$(CMAKE)" --build $(BUILD_DIR) --target ndfs_tests
	"$(CTEST)" --test-dir $(BUILD_DIR) --output-on-failure

## test-ts: run the TypeScript test suite
test-ts ts:
	cd ndfs-ts && npm install && npm test

## test-py: run the Python test suite
test-py py:
	cd ndfs-py && PYTHONPATH=src python -m pytest tests/ -v

# ── Maintenance ─────────────────────────────────────────────────────

## install: install ndtool to PREFIX (default /usr/local)
PREFIX ?= /usr/local
install: ndtool
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 "$(NDTOOL)" "$(DESTDIR)$(PREFIX)/bin/"

## clean: remove the C build directory
clean:
	rm -rf $(BUILD_DIR)

## distclean: clean everything, including node_modules and Python caches
distclean: clean
	rm -rf ndfs-ts/node_modules ndfs-py/.pytest_cache
	find . -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true

## format: run clang-format over the C sources (if installed)
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	find $(C_DIR)/src $(C_DIR)/include $(C_DIR)/tools -name '*.c' -o -name '*.h' \
		| xargs clang-format -i

## help: list available targets
help:
	@echo "norskdata-ndfs - make targets:"
	@grep -E '^## ' $(MAKEFILE_LIST) | sed -e 's/## /  /' | sort
