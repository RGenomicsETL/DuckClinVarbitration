source_root <- tempfile("rclinvar-download-source-")
cache_dir <- tempfile("rclinvar-download-cache-")
dir.create(file.path(source_root, "xml"), recursive = TRUE)
dir.create(file.path(source_root, "tab_delimited", "archive"), recursive = TRUE)

write_remote <- function(relative_path, value, checksum = TRUE) {
  path <- file.path(source_root, relative_path)
  dir.create(dirname(path), recursive = TRUE, showWarnings = FALSE)
  writeBin(charToRaw(value), path)
  if (checksum) {
    writeLines(
      paste(unname(tools::md5sum(path)), paste0("*", basename(path))),
      paste0(path, ".md5")
    )
  }
  path
}

latest_xml_name <- "ClinVarVCVRelease_00-latest.xml.gz"
latest_xml <- write_remote(file.path("xml", latest_xml_name), "latest-v1")
archive_xml_name <- "ClinVarVCVRelease_2026-03.xml.gz"
write_remote(file.path("xml", archive_xml_name), "archive-v1")
old_archive_xml_name <- "ClinVarVCVRelease_2024-03.xml.gz"
write_remote(
  file.path("xml", "archive", "2024", old_archive_xml_name),
  "old-archive-v1"
)
write_remote(file.path("tab_delimited", "submission_summary.txt.gz"), "submission-latest")
write_remote(file.path("tab_delimited", "variant_summary.txt.gz"), "variant-latest")
write_remote(
  file.path("tab_delimited", "archive", "submission_summary_2026-03.txt.gz"),
  "submission-archive", checksum = FALSE
)
write_remote(
  file.path("tab_delimited", "archive", "variant_summary_2026-03.txt.gz"),
  "variant-archive", checksum = FALSE
)

old_option <- getOption("RClinVarbitration.ncbi_base_url")
source_url_path <- normalizePath(source_root, winslash = "/", mustWork = TRUE)
if (.Platform$OS.type == "windows") source_url_path <- paste0("/", source_url_path)
options(RClinVarbitration.ncbi_base_url = paste0("file://", source_url_path))

path <- rclinvarbitration_download_clinvar(cache_dir = cache_dir, quiet = TRUE)
expect_true(file.exists(unname(path)))
expect_equal(readChar(unname(path), file.info(unname(path))$size), "latest-v1")
expect_false(attr(path, "download")$cache_hit)

cached <- rclinvarbitration_download_clinvar(cache_dir = cache_dir, quiet = TRUE)
expect_true(attr(cached, "download")$cache_hit)
expect_equal(as.character(cached), as.character(path))

write_remote(file.path("xml", latest_xml_name), "latest-v2")
refreshed <- rclinvarbitration_download_clinvar(cache_dir = cache_dir, quiet = TRUE)
expect_false(attr(refreshed, "download")$cache_hit)
expect_equal(readChar(unname(refreshed), file.info(unname(refreshed))$size), "latest-v2")

archived <- rclinvarbitration_download_clinvar(
  release = "2026-03", cache_dir = cache_dir, quiet = TRUE
)
expect_equal(basename(unname(archived)), archive_xml_name)
expect_equal(readChar(unname(archived), file.info(unname(archived))$size), "archive-v1")
old_archived <- rclinvarbitration_download_clinvar(
  release = "2024-03", cache_dir = cache_dir, quiet = TRUE
)
expect_equal(basename(unname(old_archived)), old_archive_xml_name)
expect_match(attr(old_archived, "download")$url, "/xml/archive/2024/")

flat <- rclinvarbitration_download_clinvar(
  file = c("submission_summary", "variant_summary"),
  cache_dir = cache_dir, quiet = TRUE
)
expect_equal(names(flat), c("submission_summary", "variant_summary"))
expect_true(all(nzchar(attr(flat, "download")$md5)))

archive_flat <- rclinvarbitration_download_clinvar(
  release = "2026-03", file = c("submission_summary", "variant_summary"),
  cache_dir = cache_dir, quiet = TRUE
)
expect_true(all(is.na(attr(archive_flat, "download")$md5)))
expect_equal(
  readChar(archive_flat[["submission_summary"]], file.info(archive_flat[["submission_summary"]])$size),
  "submission-archive"
)

expect_error(rclinvarbitration_download_clinvar(release = "2026-13"), "YYYY-MM")
expect_error(rclinvarbitration_download_clinvar(file = "unknown"), "unique values")

options(RClinVarbitration.ncbi_base_url = old_option)
unlink(c(source_root, cache_dir), recursive = TRUE, force = TRUE)
