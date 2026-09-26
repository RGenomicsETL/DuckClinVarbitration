#!/usr/bin/env Rscript

if (!requireNamespace("litedown", quietly = TRUE)) {
  stop("Package 'litedown' is required", call. = FALSE)
}

readme <- readLines("README.md", warn = FALSE, encoding = "UTF-8")
title_heading <- which(readme == "# DuckClinVarbitration")
if (length(title_heading)) {
  readme <- readme[-title_heading[[1L]]]
}

landing_css <- normalizePath(
  "tools/landing.css",
  winslash = "/",
  mustWork = TRUE
)
landing_header <- normalizePath(
  "tools/landing-header.html",
  winslash = "/",
  mustWork = TRUE
)

metadata <- c(
  "---",
  "title: DuckClinVarbitration",
  "output:",
  "  html:",
  "    options:",
  "      toc: true",
  "    meta:",
  paste0(
    "      css: [\"@default@1.14.69\", \"@article@1.14.69\", ",
    "\"@site@1.14.69\", \"",
    landing_css,
    "\"]"
  ),
  paste0("      include_before: \"", landing_header, "\""),
  "---"
)

dir.create("_site", recursive = TRUE, showWarnings = FALSE)
litedown::mark(
  text = c(metadata, readme),
  output = file.path("_site", "index.html")
)
