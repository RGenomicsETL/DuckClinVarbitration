# Native dependency boundary

`src/duckclinvarbitration_extension.c` implements the DuckDB C entrypoint and
streaming ClinVar/PubMed readers with libxml2 `xmlTextReader*` and zlib `gz*`.
`duckdb_capi/v1.5.0/` through `v1.5.6/` contain pinned exact-version DuckDB
headers and their `duckdb_headers.json` checksums. The `v1.5.0` through `v1.5.5`
artifacts use `C_STRUCT_UNSTABLE` metadata; never load one into another engine
version. DuckDB stabilized every function this extension uses in `v1.5.6`, so
the `v1.5.6` artifact carries `C_STRUCT` metadata recording C API `v1.5.6`,
which DuckDB negotiates for every newer engine, including the v2.0 line
(`C_STRUCT_UNSTABLE` is the v2 entrypoint there, so the unstable artifacts do
not load). `duckdb_capi/stable_capi.txt` names the stable floor.
`tools/fetch_duckdb_headers.R --ref vMAJOR.MINOR.PATCH` updates headers explicitly.

`src/rclinvarbitration_clinvar_core.inc` and
`src/rclinvarbitration_pubmed_core.inc` hold the engine-agnostic XML scanners.
Two hosts include them: `src/duckclinvarbitration_extension.c` for the v1 C API
and `src/duckclinvarbitration_extension_v2.c` for the stable C API v2
(`duckdb_capi_v2/`, revision pinned in `duckdb_capi_v2/REVISION`). The v2 host
emits a `C_STRUCT` artifact recording C API `v2.0.0`, which DuckDB v2.0 and
later dispatch to `duckclinvarbitration_init_c_api_v2`. `configure` builds it
alongside the v1 artifacts; set `RCLINVAR_DUCKDB_V2=no` to skip it (the webR
wasm build does, since its engine still loads the stable v1 artifact).

The standalone CMake build resolves static libxml2 and zlib through vcpkg.
The libxml2 port exposes optional `iconv` and `zlib` features; both are off
here. Its portfile has no HTTP, FTP, Python or LZMA feature toggles. In the
local libxml2 2.15.1 vcpkg build, the CMake cache reports HTTP and Python
OFF; FTP and LZMA are not CMake options in that port. The extension does not
call those interfaces. vcpkg's static PIC archives are required on ELF
platforms; a distribution's non-PIC `libxml2.a` cannot make a shared module.
Only `duckclinvarbitration_init_c_api` (and, for the v2 host,
`duckclinvarbitration_init_c_api_v2`) is exported on ELF; the symbol guard
checks the resulting shared object.

The R front end stages the native sources and pinned headers using
`r/RClinVarbitration/bootstrap.R` and builds artifacts offline for each
supported DuckDB version. Its `configure` and `configure.win` use host
`pkg-config` for libxml2 and zlib; Rtools supplies static builds on Windows.
