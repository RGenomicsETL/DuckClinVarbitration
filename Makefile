.DEFAULT_GOAL := all
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
EXTENSION_NAME=duckclinvarbitration
USE_UNSTABLE_C_API=1
TARGET_DUCKDB_VERSION ?= v1.5.2
DUCKDB_TEST_VERSION ?= 1.5.2
EXTENSION_VERSION := $(shell sed -n 's/^version:[[:space:]]*//p' description.yml)

include extension-ci-tools/makefiles/c_api_extensions/base.Makefile
include extension-ci-tools/makefiles/c_api_extensions/c_cpp.Makefile

.PHONY: all test test_release test_debug test-extension-symbols readme site
all: configure release
configure: venv platform extension_version
build_extension_with_metadata_release build_extension_with_metadata_debug: extension_version
release: build_extension_library_release build_extension_with_metadata_release
debug: build_extension_library_debug build_extension_with_metadata_debug
# CI builds in its container, then runs test_release on the host against those
# artifacts; test_release must not rebuild. Local `make test` builds first.
test: release
	$(MAKE) test_release

test_release: test_extension_release test-extension-symbols

test_debug: test_extension_debug

DUCKDB_CLI ?= build/tools/duckdb
readme: release
	mkdir -p build/rlib
	R CMD INSTALL --no-test-load -l build/rlib r/RClinVarbitration
	R_LIBS="$(abspath build/rlib):$$R_LIBS" DUCKDB_CLI="$(abspath $(DUCKDB_CLI))" Rscript scripts/render-readme.R

site:
	Rscript scripts/build-site.R

test-extension-symbols:
	@set -e; case "$$(uname -s)" in \
	  Darwin) nm -gU build/release/lib$(EXTENSION_NAME).dylib | awk '{print $$NF}' | sed 's/^_//' ;; \
	  MINGW*|MSYS*|CYGWIN*|Windows_NT) objdump -p build/release/$(EXTENSION_NAME).duckdb_extension | \
	    awk '/\[Ordinal\/Name Pointer\] Table/ {t=1; next} t && /^\t\[ *[0-9]+\]/ {print $$NF; next} t && NF == 0 {t=0}' ;; \
	  *) \
	    nm -D --defined-only build/release/lib$(EXTENSION_NAME).so | awk '{print $$3}' ;; \
	  esac | awk 'BEGIN { bad=0; entry=0 } \
	    $$0 == "duckclinvarbitration_init_c_api" {entry++; next} \
	    /^(xml|gz|deflate|inflate|zlib)/ {print "leaked dependency symbol: " $$0; bad=1} \
	    END {if (entry != 1) {print "expected one extension entrypoint, found " entry; bad=1}; exit bad}'
