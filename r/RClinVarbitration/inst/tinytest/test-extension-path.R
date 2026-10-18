# Stable C API artifacts (ABI type C_STRUCT) cover every engine from the floor
# release upward, so one artifact per stable generation serves engines with no
# exact bundle entry. From the DuckDB v2.0 line onward the package also bundles
# a dedicated C API v2 host, which resolution prefers over the floor artifact.

if (!requireNamespace("duckdb", quietly = TRUE)) {
  exit_file("duckdb is not installed")
}

platform_con <- DBI::dbConnect(duckdb::duckdb())
platform <- DBI::dbGetQuery(platform_con, "PRAGMA platform")$platform
DBI::dbDisconnect(platform_con, shutdown = TRUE)

manifest <- system.file(
  "ext", "duckdb_capi", "versions.txt",
  package = "RClinVarbitration", mustWork = TRUE
)
supported <- trimws(readLines(manifest, warn = FALSE))
supported <- supported[nzchar(supported) & !startsWith(supported, "#")]
floor <- RClinVarbitration:::rclinvarbitration_stable_capi_version()

if (is.null(floor)) {
  exit_file("the bundled manifest declares no stable C API floor")
}

stable <- supported[
  RClinVarbitration:::rclinvarbitration_release_version(supported) >=
    RClinVarbitration:::rclinvarbitration_release_version(floor)
]
if (!length(stable)) exit_file("no stable C API artifact is bundled")

root <- system.file("ext", "build", package = "RClinVarbitration", mustWork = TRUE)
bundled <- RClinVarbitration:::rclinvarbitration_bundled_duckdb_versions(root)

# The stable floor release resolves to its own C_STRUCT artifact.
floor_path <- rclinvarbitration_extension_path(floor, platform)
expect_equal(basename(dirname(dirname(floor_path))), floor)

# Engines between the floor and the next bundle entry reuse the floor artifact.
expect_equal(
  rclinvarbitration_extension_path("v1.5.7", platform),
  floor_path
)

# A far-future engine keeps working with the newest bundled artifact.
expect_equal(
  rclinvarbitration_extension_path("v9.9.9", platform),
  rclinvarbitration_extension_path(utils::tail(bundled, 1L), platform)
)

if ("v2.0.0" %in% bundled) {
  # The dedicated C API v2 host is selected for the DuckDB v2.0 line, including
  # development builds that report a suffix.
  v2_path <- normalizePath(
    file.path(root, "v2.0.0", platform, "duckclinvarbitration.duckdb_extension"),
    mustWork = TRUE
  )
  expect_equal(rclinvarbitration_extension_path("v2.0.0", platform), v2_path)
  expect_equal(rclinvarbitration_extension_path("v2.0.0-dev83386", platform), v2_path)
  expect_equal(rclinvarbitration_extension_path("v2.7.3", platform), v2_path)
}

# An engine older than every bundled release has no artifact.
expect_error(rclinvarbitration_extension_path("v1.0.0", platform), "no artifact")
# Non-release version strings are rejected outright.
expect_error(
  rclinvarbitration_extension_path("not-a-version", platform),
  "exact release"
)
