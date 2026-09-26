# Locate a version-matched RClinVarbitration DuckDB extension

The package uses DuckDB's unstable C extension ABI for its streaming
table function, so it bundles one artifact per supported exact engine
release.

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
