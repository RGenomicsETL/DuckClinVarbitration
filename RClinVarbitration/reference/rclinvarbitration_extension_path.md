# Locate a version-matched RClinVarbitration DuckDB extension

Releases up to the stable C API floor are built against DuckDB's
exact-version `C_STRUCT_UNSTABLE` ABI, so the package bundles one
artifact per supported engine release. The floor release itself
(`v1.5.6`) uses the stable `C_STRUCT` ABI, which DuckDB negotiates for
every newer engine. From DuckDB v2.0 the package also bundles a
dedicated stable C API v2 host (`v2.0.0`, ABI type `C_STRUCT`);
resolution prefers that host for the v2.0 line and otherwise falls back
to the newest stable artifact that is no newer than the running engine.

## Usage

``` r
rclinvarbitration_extension_path(duckdb_version = NULL, duckdb_platform = NULL)
```

## Arguments

- duckdb_version:

  Exact DuckDB version, with or without a `v` prefix. `NULL` selects the
  version reported by the installed `duckdb` package.

- duckdb_platform:

  Exact platform reported by `PRAGMA platform`. `NULL` queries a
  temporary connection to the installed `duckdb` package. DuckDB
  distinguishes `windows_amd64` from R-devel's `windows_amd64_mingw`
  even though both artifacts contain the same MinGW-built machine code.

## Value

An absolute extension path.
