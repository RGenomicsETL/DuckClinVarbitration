.DEFAULT_GOAL := all
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
EXTENSION_NAME=duckclinvarbitration
USE_UNSTABLE_C_API=1
TARGET_DUCKDB_VERSION ?= v1.5.2
DUCKDB_TEST_VERSION ?= 1.5.2
EXTENSION_VERSION := $(shell sed -n 's/^version:[[:space:]]*//p' description.yml)

include extension-ci-tools/makefiles/c_api_extensions/base.Makefile
include extension-ci-tools/makefiles/c_api_extensions/c_cpp.Makefile

.PHONY: all test test_release test_debug test-extension-symbols readme
all: configure release
configure: venv platform extension_version
build_extension_with_metadata_release build_extension_with_metadata_debug: extension_version
release: build_extension_library_release build_extension_with_metadata_release
debug: build_extension_library_debug build_extension_with_metadata_debug
test: test_release test-extension-symbols

test_release: release
	$(MAKE) test_extension_release

test_debug: debug
	$(MAKE) test_extension_debug

DUCKDB_CLI ?= build/tools/duckdb
readme: release
	DUCKDB_CLI="$(abspath $(DUCKDB_CLI))" Rscript -e 'rmarkdown::render("README.Rmd", output_file = "README.md", quiet = TRUE)'

test-extension-symbols: release
	@set -e; if [ "$$(uname -s)" = Darwin ]; then \
	    nm -gU build/release/lib$(EXTENSION_NAME).dylib | awk '{print $$NF}' | sed 's/^_//'; \
	  else \
	    nm -D --defined-only build/release/lib$(EXTENSION_NAME).so | awk '{print $$3}'; \
	  fi | awk 'BEGIN { bad=0; entry=0 } \
	    $$0 == "duckclinvarbitration_init_c_api" {entry++; next} \
	    /^(xml|gz|deflate|inflate|zlib)/ {print "leaked dependency symbol: " $$0; bad=1} \
	    END {if (entry != 1) {print "expected one extension entrypoint, found " entry; bad=1}; exit bad}'
