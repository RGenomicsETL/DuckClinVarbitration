# Native dependency boundary

`src/duckclinvarbitration_extension.c` implements the DuckDB C entrypoint and
streaming ClinVar/PubMed readers with libxml2 `xmlTextReader*` and zlib `gz*`.
`duckdb_capi/v1.5.0/` through `v1.5.5/` contain pinned exact-version DuckDB
headers and their `duckdb_headers.json` checksums. The extension uses
`C_STRUCT_UNSTABLE` metadata; never load it into another engine version.
`tools/fetch_duckdb_headers.R --ref vMAJOR.MINOR.PATCH` updates headers explicitly.

The standalone CMake build resolves static libxml2 and zlib through vcpkg.
The libxml2 port exposes optional `iconv` and `zlib` features; both are off
here. Its portfile has no separate HTTP, FTP, Python or LZMA feature toggles;
those settings follow the port's upstream CMake defaults. The extension does
not call those interfaces. vcpkg's static PIC archives are required on ELF
platforms; a distribution's non-PIC `libxml2.a` cannot make a shared module.
Only `duckclinvarbitration_init_c_api` is exported on ELF; the symbol guard
checks the resulting shared object.

The R front end stages the native sources and pinned headers using
`r/RClinVarbitration/bootstrap.R` and builds artifacts offline for each
supported DuckDB version. Its `configure` and `configure.win` use host
`pkg-config` for libxml2 and zlib; Rtools supplies static builds on Windows.
