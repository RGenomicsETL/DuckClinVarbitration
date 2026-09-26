#!/usr/bin/env Rscript
args <- commandArgs(trailingOnly = TRUE)
`%||%` <- function(x, y) if (is.null(x)) y else x

parse_args <- function(x) {
  out <- list()
  index <- 1L
  while (index <= length(x)) {
    if (startsWith(x[[index]], "--") && index < length(x)) {
      out[[substring(x[[index]], 3L)]] <- x[[index + 1L]]
      index <- index + 2L
    } else {
      index <- index + 1L
    }
  }
  out
}

signature <- function() c(as.raw(c(0, 147, 4, 16)), charToRaw("duckdb_signature"), as.raw(c(128, 4)))
padded <- function(x) {
  bytes <- charToRaw(x)
  if (length(bytes) > 32L) stop("extension metadata field exceeds 32 bytes", call. = FALSE)
  c(bytes, as.raw(rep(0, 32L - length(bytes))))
}

options <- parse_args(args)
required <- c("library-file", "out-file", "extension-name", "duckdb-platform", "duckdb-version", "extension-version", "abi-type")
missing <- required[!vapply(required, function(name) nzchar(options[[name]] %||% ""), logical(1))]
if (length(missing)) stop("missing required argument(s): ", paste(missing, collapse = ", "), call. = FALSE)
if (!identical(options[["abi-type"]], "C_STRUCT_UNSTABLE")) stop("only C_STRUCT_UNSTABLE metadata is supported", call. = FALSE)

temporary <- paste0(options[["out-file"]], ".tmp")
on.exit(unlink(temporary), add = TRUE)
if (!file.copy(options[["library-file"]], temporary, overwrite = TRUE)) stop("failed to copy extension library", call. = FALSE)
connection <- file(temporary, open = "ab")
on.exit(close(connection), add = TRUE)
writeBin(signature(), connection, useBytes = TRUE)
writeBin(padded(""), connection, useBytes = TRUE)
writeBin(padded(""), connection, useBytes = TRUE)
writeBin(padded(""), connection, useBytes = TRUE)
writeBin(padded(options[["abi-type"]]), connection, useBytes = TRUE)
writeBin(padded(options[["extension-version"]]), connection, useBytes = TRUE)
writeBin(padded(options[["duckdb-version"]]), connection, useBytes = TRUE)
writeBin(padded(options[["duckdb-platform"]]), connection, useBytes = TRUE)
writeBin(padded("4"), connection, useBytes = TRUE)
writeBin(as.raw(rep(0, 256L)), connection, useBytes = TRUE)
close(connection)
if (!file.rename(temporary, options[["out-file"]])) stop("failed to finalize extension metadata", call. = FALSE)
