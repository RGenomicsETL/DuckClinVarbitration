rclinvarbitration_reproduction_sql <- function(
    submission_sql, variant_sql, assembly, submitter_exclusion_predicate) {
  contig <- if (identical(assembly, "GRCh38")) {
    "CASE WHEN chromosome = 'MT' THEN 'chrM' ELSE 'chr' || chromosome END"
  } else {
    "chromosome"
  }
  contigs <- if (identical(assembly, "GRCh38")) {
    c(paste0("chr", 1:22), "chrX", "chrY", "chrM", "chrMT")
  } else {
    c(as.character(1:22), "X", "Y", "M", "MT")
  }
  contigs_sql <- paste(rclinvarbitration_sql_string(contigs), collapse = ", ")
  paste0(
    "WITH variants_read AS (SELECT *, row_number() OVER () AS source_ordinal ",
    "FROM read_csv(", variant_sql,
    ", header = true, delim = '\\t', quote = '', all_varchar = true)),",
    "usable_locations AS (SELECT cast(\"#AlleleID\" AS INTEGER) AS allele_id,",
    "try_cast(\"VariationID\" AS UBIGINT) AS variation_id, ",
    contig, " AS contig, try_cast(\"PositionVCF\" AS INTEGER) AS position,",
    "\"ReferenceAlleleVCF\" AS reference, \"AlternateAlleleVCF\" AS alternate, source_ordinal ",
    "FROM variants_read WHERE \"Assembly\" = ", rclinvarbitration_sql_string(assembly),
    " AND \"Chromosome\" IS NOT NULL AND \"PositionVCF\" IS NOT NULL ",
    "AND \"ReferenceAlleleVCF\" IS NOT NULL AND \"AlternateAlleleVCF\" IS NOT NULL ",
    "AND \"ReferenceAlleleVCF\" <> 'na' AND \"AlternateAlleleVCF\" <> 'na' ",
    "AND \"ReferenceAlleleVCF\" <> \"AlternateAlleleVCF\" ",
    "AND length(\"ReferenceAlleleVCF\") + length(\"AlternateAlleleVCF\") <= 40 ",
    "AND regexp_matches(\"ReferenceAlleleVCF\", '^[ACGTN]+') ",
    "AND regexp_matches(\"AlternateAlleleVCF\", '^[ACGTN]+')),",
    "locations AS (SELECT * FROM usable_locations WHERE contig IN (", contigs_sql, ") ",
    "QUALIFY row_number() OVER (PARTITION BY contig, variation_id ORDER BY source_ordinal DESC) = 1),",
    "submissions_read AS (SELECT *, row_number() OVER () AS source_ordinal ",
    "FROM read_csv(", submission_sql,
    ", skip = 18, header = true, delim = '\\t', quote = '', all_varchar = true)),",
    "classified AS (SELECT try_cast(\"#VariationID\" AS UBIGINT) AS variation_id,",
    "CASE WHEN \"DateLastEvaluated\" = '-' THEN DATE '1970-01-01' ",
    "ELSE try_strptime(\"DateLastEvaluated\", '%b %d, %Y')::DATE END AS date_last_evaluated,",
    "lower(trim(coalesce(\"Submitter\", ''))) AS submitter_normalized,",
    "lower(trim(coalesce(\"ReviewStatus\", ''))) AS review_status_normalized, source_ordinal,",
    "CASE \"ClinicalSignificance\" ",
    "WHEN 'Pathogenic' THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN 'Likely pathogenic' THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN 'Pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN 'Likely pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN 'Pathogenic/Likely pathogenic' THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN 'Benign' THEN 'Benign' WHEN 'Likely benign' THEN 'Benign' ",
    "WHEN 'Benign/Likely benign' THEN 'Benign' WHEN 'protective' THEN 'Benign' ",
    "WHEN 'Uncertain significance' THEN 'VUS' WHEN 'Uncertain risk allele' THEN 'VUS' ",
    "ELSE 'Unknown' END AS classification_bin ",
    "FROM submissions_read WHERE try_cast(\"#VariationID\" AS UBIGINT) IN ",
    "(SELECT DISTINCT variation_id FROM locations)),",
    "eligible AS (SELECT * FROM classified WHERE classification_bin <> 'Unknown' ",
    "AND NOT (classification_bin = 'Benign' ",
    "AND submitter_normalized = 'illumina laboratory services; illumina') ",
    "AND ", submitter_exclusion_predicate, "),",
    "dated AS (SELECT *, (date_last_evaluated >= DATE '2016-01-01' ",
    "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel')) AS is_modern,",
    "max(CASE WHEN date_last_evaluated >= DATE '2016-01-01' ",
    "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel') ",
    "THEN 1 ELSE 0 END) OVER (PARTITION BY variation_id) AS has_modern FROM eligible),",
    "retained AS (SELECT * FROM dated WHERE has_modern = 0 OR is_modern),",
    "summarized AS (SELECT variation_id, count(*) AS retained_submission_count,",
    "count(*) FILTER (WHERE classification_bin = 'Pathogenic/Likely Pathogenic') AS pathogenic_count,",
    "count(*) FILTER (WHERE classification_bin = 'Benign') AS benign_count,",
    "count(*) FILTER (WHERE classification_bin = 'VUS') AS uncertain_count,",
    "min_by(classification_bin, source_ordinal) FILTER (WHERE review_status_normalized ",
    "IN ('practice guideline', 'reviewed by expert panel')) AS strong_review_classification,",
    "max(CASE WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign') ",
    "AND review_status_normalized = 'practice guideline' THEN 4 ",
    "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign') ",
    "AND review_status_normalized = 'reviewed by expert panel' THEN 3 ",
    "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign') ",
    "AND review_status_normalized <> 'no assertion criteria provided' THEN 1 ELSE 0 END) AS gold_stars ",
    "FROM retained GROUP BY variation_id),",
    "decisions AS (SELECT *, CASE ",
    "WHEN strong_review_classification IS NOT NULL THEN strong_review_classification ",
    "WHEN pathogenic_count > 0 AND benign_count > 0 AND ",
    "greatest(pathogenic_count, benign_count) >= retained_submission_count * 0.6 AND ",
    "least(pathogenic_count, benign_count) <= retained_submission_count * 0.2 ",
    "THEN CASE WHEN benign_count > pathogenic_count THEN 'Benign' ",
    "ELSE 'Pathogenic/Likely Pathogenic' END ",
    "WHEN pathogenic_count > 0 AND benign_count > 0 THEN 'Conflicting' ",
    "WHEN uncertain_count > retained_submission_count * 0.6 THEN 'VUS' ",
    "WHEN pathogenic_count > 0 THEN 'Pathogenic/Likely Pathogenic' ",
    "WHEN benign_count > 0 THEN 'Benign' ELSE 'VUS' END AS clinical_significance ",
    "FROM summarized) ",
    "SELECT l.contig, l.position, l.reference, l.alternate, d.clinical_significance, ",
    "cast(d.gold_stars AS INTEGER) AS gold_stars, l.allele_id ",
    "FROM locations l JOIN decisions d USING (variation_id)"
  )
}

#' Reproduce ClinVarbitration decisions from archived ClinVar flat files
#'
#' Reproduces Centre for Population Genomics ClinVarbitration 2.2.11's
#' allele-level decision algorithm directly from NCBI's versioned
#' `submission_summary` and `variant_summary` files. The generated Parquet has
#' the exact seven-column `clinvar_decisions.tsv` layout: `contig`, `position`,
#' `reference`, `alternate`, `clinical_significance`, `gold_stars`, and
#' `allele_id`.
#'
#' This is separate from [rclinvarbitration_import_xml()] and the disease-aware
#' policy views. It exists to reproduce and differentially validate the upstream
#' allele-level artifact on matching archived flat-file releases. It uses
#' DuckDB's streaming CSV reader and SQL aggregation; it does not invoke Python,
#' Hail, VEP, or PM5 logic.
#'
#' @param con A DuckDB DBI connection.
#' @param submission_path Archived NCBI `submission_summary_YYYY-MM.txt.gz`.
#' @param variant_path Archived NCBI `variant_summary_YYYY-MM.txt.gz`.
#' @param path New output `.parquet` path.
#' @param assembly Genome assembly: `"GRCh38"` or `"GRCh37"`.
#' @param submitter_exclusions Submitter names to exclude for a blinded run.
#' @return A named list describing the written Parquet file, invisibly.
#' @export
rclinvarbitration_reproduce_clinvarbitration_parquet <- function(
    con, submission_path, variant_path, path,
    assembly = c("GRCh38", "GRCh37"), submitter_exclusions = character()) {
  for (argument in list(submission_path = submission_path, variant_path = variant_path)) {
    if (!is.character(argument) || length(argument) != 1L || is.na(argument) || !file.exists(argument)) {
      stop("`submission_path` and `variant_path` must name existing files.", call. = FALSE)
    }
  }
  if (!is.character(path) || length(path) != 1L || is.na(path) || !nzchar(path)) {
    stop("`path` must be a non-empty output file path.", call. = FALSE)
  }
  excluded_submitters <- rclinvarbitration_normalize_submitter_exclusions(submitter_exclusions)
  assembly <- match.arg(assembly)
  path <- normalizePath(path, mustWork = FALSE)
  if (!dir.exists(dirname(path))) {
    stop("The parent directory of `path` does not exist.", call. = FALSE)
  }
  if (file.exists(path)) {
    stop("`path` already exists; refuse to overwrite it.", call. = FALSE)
  }

  submitter_exclusion_predicate <- if (length(excluded_submitters)) {
    paste0(
      "submitter_normalized NOT IN (",
      paste(rclinvarbitration_sql_string(excluded_submitters), collapse = ", "), ")"
    )
  } else {
    "TRUE"
  }
  select_sql <- rclinvarbitration_reproduction_sql(
    submission_sql = rclinvarbitration_sql_string(normalizePath(submission_path)),
    variant_sql = rclinvarbitration_sql_string(normalizePath(variant_path)),
    assembly = assembly,
    submitter_exclusion_predicate = submitter_exclusion_predicate
  )
  DBI::dbExecute(
    con,
    paste0(
      "COPY (", select_sql, ") TO ", rclinvarbitration_sql_string(path),
      " (FORMAT PARQUET, COMPRESSION ZSTD)"
    )
  )
  n_rows <- DBI::dbGetQuery(
    con,
    paste0("SELECT count(*) AS n FROM read_parquet(", rclinvarbitration_sql_string(path), ")")
  )$n[[1L]]
  invisible(list(
    path = path,
    rows = n_rows,
    assembly = assembly,
    submitter_exclusions = excluded_submitters,
    policy_version = rclinvarbitration_policy_version()
  ))
}
