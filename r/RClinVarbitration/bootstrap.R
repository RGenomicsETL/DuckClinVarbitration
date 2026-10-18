#!/usr/bin/env Rscript
# Stage the extension sources and pinned C API headers into the R package.
args <- commandArgs(trailingOnly = TRUE)
if (length(args) != 1L) {
  stop("Usage: Rscript bootstrap.R /path/to/duckclinvarbitration", call. = FALSE)
}
repo <- normalizePath(args[[1L]], mustWork = TRUE)
package <- getwd()
if (!file.exists(file.path(package, "DESCRIPTION")) ||
    !file.exists(file.path(repo, "src", "duckclinvarbitration_extension.c"))) {
  stop("Run bootstrap.R from r/RClinVarbitration with the repository root as argument",
       call. = FALSE)
}
destination <- file.path(package, "inst", "ext")
dir.create(destination, recursive = TRUE, showWarnings = FALSE)
for (name in c(
  "duckclinvarbitration_extension.c",
  "duckclinvarbitration_extension_v2.c",
  "rclinvarbitration_clinvar_core.inc",
  "rclinvarbitration_pubmed_core.inc"
)) {
  if (!file.copy(file.path(repo, "src", name), file.path(destination, name),
                 overwrite = TRUE)) stop("Could not stage ", name, call. = FALSE)
}
if (!file.copy(file.path(repo, "tools", "append_extension_metadata.R"),
               file.path(destination, "append_extension_metadata.R"), overwrite = TRUE)) {
  stop("Could not stage metadata writer", call. = FALSE)
}
headers <- file.path(destination, "duckdb_capi")
dir.create(headers, showWarnings = FALSE)
for (version in readLines(file.path(repo, "duckdb_capi", "versions.txt"))) {
  if (!nzchar(version) || startsWith(version, "#")) next
  directory <- file.path(headers, version)
  dir.create(directory, showWarnings = FALSE)
  names <- c("duckdb.h", "duckdb_extension.h", "duckdb_headers.json")
  if (!all(file.copy(file.path(repo, "duckdb_capi", version, names),
                     file.path(directory, names), overwrite = TRUE))) {
    stop("Could not stage headers for ", version, call. = FALSE)
  }
}
if (!file.copy(file.path(repo, "duckdb_capi", "versions.txt"),
               file.path(headers, "versions.txt"), overwrite = TRUE)) {
  stop("Could not stage header version manifest", call. = FALSE)
}
if (!file.copy(file.path(repo, "duckdb_capi", "stable_capi.txt"),
               file.path(headers, "stable_capi.txt"), overwrite = TRUE)) {
  stop("Could not stage stable C API manifest", call. = FALSE)
}

# The DuckDB C API v2 SDK powers the optional v2 host. Stage it alongside the
# v1 headers so configure can build a C_STRUCT v2.0.0 artifact.
v2_headers <- file.path(destination, "duckdb_capi_v2")
dir.create(v2_headers, showWarnings = FALSE)
v2_names <- c("duckdb_v2.h", "duckdb_extension_v2.h", "duckdb_headers.json", "REVISION")
if (!all(file.copy(file.path(repo, "duckdb_capi_v2", v2_names),
                   file.path(v2_headers, v2_names), overwrite = TRUE))) {
  stop("Could not stage DuckDB C API v2 headers", call. = FALSE)
}
