#!/usr/bin/env Rscript

site <- if (length(commandArgs(TRUE))) commandArgs(TRUE)[[1L]] else "http://127.0.0.1:8765/"
site <- paste0(sub("/+$", "", site), "/")
root <- "https://rgenomicsetl.github.io/DuckClinVarbitration/"

fetch <- function(path) {
  response <- curl::curl_fetch_memory(paste0(site, path))
  if (response$status_code != 200L) {
    stop("HTTP ", response$status_code, " for ", path)
  }
  xml2::read_html(rawToChar(response$content))
}

page <- fetch("")
links <- xml2::xml_attr(xml2::xml_find_all(page, "//a[@href]"), "href")
checked <- character()
for (link in links) {
  if (startsWith(link, root)) link <- substring(link, nchar(root) + 1L)
  if (grepl("^(https?:|mailto:)", link)) next
  parts <- strsplit(link, "#", fixed = TRUE)[[1L]]
  path <- if (length(parts)) parts[[1L]] else ""
  target <- fetch(path)
  if (length(parts) > 1L) {
    anchor <- URLdecode(parts[[2L]])
    ids <- xml2::xml_attr(xml2::xml_find_all(target, "//*[@id]"), "id")
    if (!anchor %in% ids) stop("Missing anchor: ", link)
  }
  checked <- c(checked, if (nzchar(link)) link else "./")
}
cat("Checked", length(checked), "local links on index.html (including nav):\n")
cat(paste(unique(checked), collapse = "\n"), "\n")
