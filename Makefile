.DEFAULT_GOAL := all
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
EXTENSION_NAME=duckclinvarbitration
USE_UNSTABLE_C_API=1
TARGET_DUCKDB_VERSION ?= v1.5.2
DUCKDB_TEST_VERSION ?= 1.5.2
EXTENSION_VERSION := $(shell sed -n 's/^version:[[:space:]]*//p' description.yml)

include extension-ci-tools/makefiles/c_api_extensions/base.Makefile
include extension-ci-tools/makefiles/c_api_extensions/c_cpp.Makefile

.PHONY: all test test-extension-symbols
all: configure release
configure: venv platform extension_version
build_extension_with_metadata_release build_extension_with_metadata_debug: extension_version
release: build_extension_library_release build_extension_with_metadata_release
debug: build_extension_library_debug build_extension_with_metadata_debug
test: release test-extension-symbols
	$(TEST_RUNNER_RELEASE)

test-extension-symbols: release
	@if command -v nm >/dev/null 2>&1 && [ -f build/release/lib$(EXTENSION_NAME).so ]; then \
	  nm -D --defined-only build/release/lib$(EXTENSION_NAME).so | awk '{print $$3}' | \
	  awk 'BEGIN { bad=0; entry=0 } \
	    $$0 == "duckclinvarbitration_init_c_api" {entry++ ; next} \
	    /^(xml|xmlTextReader|gz|deflate|inflate|zlib)/ {print "leaked dependency symbol: " $$0; bad=1} \
	    END {if (entry != 1) {print "expected one extension entrypoint, found " entry; bad=1}; exit bad}'; \
	else echo 'symbol guard requires Linux nm and a release shared object'; exit 1; fi
