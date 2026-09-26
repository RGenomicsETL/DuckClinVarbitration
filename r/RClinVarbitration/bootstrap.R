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
for (name in c("duckclinvarbitration_extension.c", "rclinvarbitration_pubmed.c")) {
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
