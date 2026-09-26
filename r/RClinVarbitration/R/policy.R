#' Current ClinVarbitration policy version
#'
#' The identifier pins the policy semantics adapted from Centre for Population
#' Genomics ClinVarbitration 2.2.11 (upstream commit
#' `658b9f241eb2d43aa11214b153b19c1e18a16337`) and records that decisions are
#' grouped per disease rather than only per variation. The identifier has no
#' package-local suffix: the view names, rather than a speculative `v1`, state
#' whether the output is disease-scoped or allele-scoped.
#'
#' @return A character scalar.
#' @export
rclinvarbitration_policy_version <- function() {
  "cpg-clinvarbitration-2.2.11"
}

rclinvarbitration_normalize_submitter_exclusions <- function(submitter_exclusions) {
  if (!is.character(submitter_exclusions) || anyNA(submitter_exclusions)) {
    stop("`submitter_exclusions` must be a character vector without missing values.", call. = FALSE)
  }
  exclusions <- tolower(trimws(submitter_exclusions))
  unique(exclusions[nzchar(exclusions)])
}

#' ClinVarbitration policy SQL
#'
#' Returns DuckDB views implementing the supported policy over
#' `clinvar_disease_submissions`. The policy bins submitted classifications,
#' excludes unknown bins and applies the upstream policy's intended qualified
#' Illumina benign exclusion, optionally applies profile-specific submitter
#' exclusions, prefers evidence
#' evaluated from 2016 onward while always retaining expert-panel and practice
#' guideline evidence, and applies the 60/20 majority rule. The disease-level
#' and allele-level views both reproduce the upstream strong-review rule: the
#' first retained practice-guideline or expert-panel classification in source
#' order is decisive.
#'
#' `clinvar_policy_pathogenic_alleles` is the disease-specific pathogenic or
#' likely-pathogenic join surface for downstream Rduckhts/DuckHTS annotation.
#' It does not perform VEP or PM5 computation.
#'
#' @param policy_version Exact supported policy version. See
#'   [rclinvarbitration_policy_version()].
#' @return A named character vector of DuckDB `CREATE OR REPLACE VIEW`
#'   statements.
#' @export
rclinvarbitration_policy_sql <- function(
    policy_version = rclinvarbitration_policy_version()) {
  supported <- rclinvarbitration_policy_version()
  if (!is.character(policy_version) || length(policy_version) != 1L ||
      is.na(policy_version) || !identical(policy_version, supported)) {
    stop(
      "Unsupported ClinVarbitration policy version. Supported version: ",
      supported,
      ".",
      call. = FALSE
    )
  }

  c(
    policy_decisions = paste(
      "CREATE OR REPLACE VIEW clinvar_policy_decisions AS WITH classified AS (",
      "SELECT p.policy_version, p.profile_id, d.*,",
      "lower(trim(coalesce(d.submitter_name, ''))) AS submitter_normalized,",
      "lower(trim(coalesce(d.review_status, ''))) AS review_status_normalized,",
      "CASE lower(trim(coalesce(d.classification, '')))",
      "WHEN 'pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN 'likely pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN 'pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN 'likely pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN 'pathogenic/likely pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN 'benign' THEN 'Benign' WHEN 'likely benign' THEN 'Benign'",
      "WHEN 'benign/likely benign' THEN 'Benign' WHEN 'protective' THEN 'Benign'",
      "WHEN 'uncertain significance' THEN 'VUS'",
      "WHEN 'uncertain risk allele' THEN 'VUS' ELSE 'Unknown' END AS classification_bin",
      "FROM clinvar_disease_submissions d JOIN clinvar_policy_profiles p",
      paste0("ON p.policy_version = '", policy_version, "'),"),
      "eligible AS (SELECT c.* FROM classified c",
      "WHERE c.classification_bin <> 'Unknown'",
      "AND NOT (c.classification_bin = 'Benign'",
      "AND c.submitter_normalized = 'illumina laboratory services; illumina')",
      "AND NOT EXISTS (SELECT 1 FROM clinvar_policy_submitter_exclusions e",
      "WHERE e.policy_version = c.policy_version AND e.profile_id = c.profile_id",
      "AND lower(trim(e.submitter_name)) = c.submitter_normalized",
      "AND (e.classification_bin IS NULL",
      "OR lower(trim(e.classification_bin)) = lower(c.classification_bin)))),",
      "deduplicated AS (SELECT * FROM eligible QUALIFY row_number() OVER (",
      "PARTITION BY policy_version, profile_id, release_id, vcv_accession, allele_id,",
      "disease_key, assertion_entity_id ORDER BY scv_version DESC NULLS LAST,",
      "assertion_id NULLS LAST, condition_id) = 1),",
      "dated AS (SELECT *,",
      "(coalesce(date_last_evaluated, DATE '1970-01-01') >= DATE '2016-01-01'",
      "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel')) AS is_modern,",
      "max(CASE WHEN coalesce(date_last_evaluated, DATE '1970-01-01') >= DATE '2016-01-01'",
      "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel')",
      "THEN 1 ELSE 0 END) OVER (PARTITION BY policy_version, profile_id, release_id,",
      "vcv_accession, allele_id, disease_key) AS has_modern,",
      "count(*) OVER (PARTITION BY policy_version, profile_id, release_id,",
      "vcv_accession, allele_id, disease_key) AS eligible_submission_count",
      "FROM deduplicated),",
      "retained AS (SELECT * FROM dated WHERE has_modern = 0 OR is_modern),",
      "summarized AS (SELECT policy_version, profile_id, release_id, vcv_accession,",
      "max(variation_id) AS variation_id, allele_id, disease_key,",
      "max(disease_database) AS disease_database,",
      "max(disease_identifier) AS disease_identifier, max(disease_name) AS disease_name,",
      "max(eligible_submission_count) AS eligible_submission_count,",
      "count(*) AS retained_submission_count,",
      "count(DISTINCT assertion_entity_id) AS retained_scv_count,",
      "count(DISTINCT submitter_normalized) AS retained_submitter_count,",
      "max(has_modern) = 1 AS modern_filter_applied,",
      "count(*) FILTER (WHERE classification_bin = 'Pathogenic/Likely Pathogenic') AS pathogenic_count,",
      "count(*) FILTER (WHERE classification_bin = 'Benign') AS benign_count,",
      "count(*) FILTER (WHERE classification_bin = 'VUS') AS uncertain_count,",
      "min_by(classification_bin, coalesce(source_ordinal, assertion_id)) FILTER",
      "(WHERE review_status_normalized IN ('practice guideline', 'reviewed by expert panel'))",
      "AS strong_review_classification,",
      "max(CASE WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
      "AND review_status_normalized = 'practice guideline' THEN 4",
      "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
      "AND review_status_normalized = 'reviewed by expert panel' THEN 3",
      "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
      "AND review_status_normalized <> 'no assertion criteria provided' THEN 1 ELSE 0 END) AS gold_stars,",
      "max(date_last_evaluated) AS latest_date_last_evaluated",
      "FROM retained GROUP BY policy_version, profile_id, release_id, vcv_accession,",
      "allele_id, disease_key),",
      "decided AS (SELECT *, CASE",
      "WHEN strong_review_classification IS NOT NULL THEN strong_review_classification",
      "WHEN pathogenic_count > 0 AND benign_count > 0 AND",
      "greatest(pathogenic_count, benign_count) >= retained_submission_count * 0.6 AND",
      "least(pathogenic_count, benign_count) <= retained_submission_count * 0.2",
      "THEN CASE WHEN benign_count > pathogenic_count THEN 'Benign'",
      "ELSE 'Pathogenic/Likely Pathogenic' END",
      "WHEN pathogenic_count > 0 AND benign_count > 0 THEN 'Conflicting'",
      "WHEN uncertain_count > retained_submission_count * 0.6 THEN 'VUS'",
      "WHEN pathogenic_count > 0 THEN 'Pathogenic/Likely Pathogenic'",
      "WHEN benign_count > 0 THEN 'Benign' ELSE 'VUS' END AS policy_classification",
      "FROM summarized)",
      "SELECT policy_version, profile_id, release_id, vcv_accession, variation_id, allele_id,",
      "disease_key, disease_database, disease_identifier, disease_name, policy_classification,",
      "gold_stars, modern_filter_applied, eligible_submission_count, retained_submission_count,",
      "retained_scv_count, retained_submitter_count, pathogenic_count, benign_count, uncertain_count,",
      "latest_date_last_evaluated FROM decided"
    ),
    policy_allele_decisions = rclinvarbitration_allele_policy_sql(policy_version),
    policy_pathogenic_alleles = paste(
      "CREATE OR REPLACE VIEW clinvar_policy_pathogenic_alleles AS SELECT",
      "d.policy_version, d.profile_id, d.release_id, d.vcv_accession, d.variation_id, d.allele_id,",
      "d.disease_key, d.disease_database, d.disease_identifier, d.disease_name,",
      "d.policy_classification, d.gold_stars, d.retained_submission_count,",
      "d.pathogenic_count, d.benign_count, d.uncertain_count,",
      "n.assembly, n.chromosome, n.position_vcf, n.reference, n.alternate, n.canonical_spdi,",
      "length(n.reference) = 1 AND length(n.alternate) = 1 AS is_snv",
      "FROM clinvar_policy_decisions d JOIN clinvar_normalized_alleles n",
      "ON n.release_id = d.release_id AND n.vcv_accession = d.vcv_accession",
      "AND n.allele_id = d.allele_id",
      "WHERE d.policy_classification = 'Pathogenic/Likely Pathogenic'"
    )
  )
}

rclinvarbitration_allele_policy_sql <- function(policy_version) {
  paste(
    "CREATE OR REPLACE VIEW clinvar_policy_allele_decisions AS",
    rclinvarbitration_allele_policy_query(policy_version)
  )
}

rclinvarbitration_allele_policy_query <- function(
    policy_version, profile_predicate = "TRUE",
    submitter_exclusion_predicate = "TRUE") {
  paste(
    "WITH classified AS (",
    "SELECT p.policy_version, p.profile_id, s.release_id, s.vcv_accession,",
    "v.variation_id, a.allele_id, s.assertion_entity_id, s.scv_accession,",
    "s.scv_version, s.assertion_id, s.source_ordinal, s.submitter_name, s.classification,",
    "s.review_status, s.date_last_evaluated,",
    "lower(trim(coalesce(s.submitter_name, ''))) AS submitter_normalized,",
    "lower(trim(coalesce(s.review_status, ''))) AS review_status_normalized,",
    "CASE lower(trim(coalesce(s.classification, '')))",
    "WHEN 'pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN 'likely pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN 'pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN 'likely pathogenic, low penetrance' THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN 'pathogenic/likely pathogenic' THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN 'benign' THEN 'Benign' WHEN 'likely benign' THEN 'Benign'",
    "WHEN 'benign/likely benign' THEN 'Benign' WHEN 'protective' THEN 'Benign'",
    "WHEN 'uncertain significance' THEN 'VUS'",
    "WHEN 'uncertain risk allele' THEN 'VUS' ELSE 'Unknown' END AS classification_bin",
    "FROM clinvar_scv_assertions s JOIN clinvar_variants v USING (release_id, vcv_accession)",
    "LEFT JOIN clinvar_alleles a ON a.release_id = s.release_id",
    "AND a.vcv_accession = s.vcv_accession AND a.parent_allele_entity_id IS NULL",
    "JOIN clinvar_policy_profiles p",
    paste0(
      "ON p.policy_version = '", policy_version, "' AND ", profile_predicate, "),"
    ),
    "eligible AS (SELECT c.* FROM classified c WHERE c.classification_bin <> 'Unknown'",
    "AND NOT (c.classification_bin = 'Benign'",
    "AND c.submitter_normalized = 'illumina laboratory services; illumina')",
    "AND NOT EXISTS (SELECT 1 FROM clinvar_policy_submitter_exclusions e",
    "WHERE e.policy_version = c.policy_version AND e.profile_id = c.profile_id",
    "AND lower(trim(e.submitter_name)) = c.submitter_normalized",
    "AND (e.classification_bin IS NULL",
    "OR lower(trim(e.classification_bin)) = lower(c.classification_bin)))",
    "AND", submitter_exclusion_predicate, "),",
    "deduplicated AS (SELECT * FROM eligible QUALIFY row_number() OVER (",
    "PARTITION BY policy_version, profile_id, release_id, vcv_accession, allele_id,",
    "assertion_entity_id ORDER BY scv_version DESC NULLS LAST, assertion_id NULLS LAST) = 1),",
    "dated AS (SELECT *,",
    "(coalesce(date_last_evaluated, DATE '1970-01-01') >= DATE '2016-01-01'",
    "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel')) AS is_modern,",
    "max(CASE WHEN coalesce(date_last_evaluated, DATE '1970-01-01') >= DATE '2016-01-01'",
    "OR review_status_normalized IN ('practice guideline', 'reviewed by expert panel')",
    "THEN 1 ELSE 0 END) OVER (PARTITION BY policy_version, profile_id, release_id,",
    "vcv_accession, allele_id) AS has_modern,",
    "count(*) OVER (PARTITION BY policy_version, profile_id, release_id,",
    "vcv_accession, allele_id) AS eligible_submission_count FROM deduplicated),",
    "retained AS (SELECT * FROM dated WHERE has_modern = 0 OR is_modern),",
    "summarized AS (SELECT policy_version, profile_id, release_id, vcv_accession,",
    "max(variation_id) AS variation_id, allele_id,",
    "max(eligible_submission_count) AS eligible_submission_count,",
    "count(*) AS retained_submission_count,",
    "count(DISTINCT assertion_entity_id) AS retained_scv_count,",
    "count(DISTINCT submitter_normalized) AS retained_submitter_count,",
    "max(has_modern) = 1 AS modern_filter_applied,",
    "count(*) FILTER (WHERE classification_bin = 'Pathogenic/Likely Pathogenic') AS pathogenic_count,",
    "count(*) FILTER (WHERE classification_bin = 'Benign') AS benign_count,",
    "count(*) FILTER (WHERE classification_bin = 'VUS') AS uncertain_count,",
    "min_by(classification_bin, coalesce(source_ordinal, assertion_id)) FILTER",
    "(WHERE review_status_normalized IN ('practice guideline', 'reviewed by expert panel'))",
    "AS strong_review_classification,",
    "max(CASE WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
    "AND review_status_normalized = 'practice guideline' THEN 4",
    "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
    "AND review_status_normalized = 'reviewed by expert panel' THEN 3",
    "WHEN classification_bin IN ('Pathogenic/Likely Pathogenic', 'Benign')",
    "AND review_status_normalized <> 'no assertion criteria provided' THEN 1 ELSE 0 END) AS gold_stars,",
    "max(date_last_evaluated) AS latest_date_last_evaluated",
    "FROM retained GROUP BY policy_version, profile_id, release_id, vcv_accession, allele_id),",
    "decided AS (SELECT *, CASE",
    "WHEN strong_review_classification IS NOT NULL THEN strong_review_classification",
    "WHEN pathogenic_count > 0 AND benign_count > 0 AND",
    "greatest(pathogenic_count, benign_count) >= retained_submission_count * 0.6 AND",
    "least(pathogenic_count, benign_count) <= retained_submission_count * 0.2",
    "THEN CASE WHEN benign_count > pathogenic_count THEN 'Benign'",
    "ELSE 'Pathogenic/Likely Pathogenic' END",
    "WHEN pathogenic_count > 0 AND benign_count > 0 THEN 'Conflicting'",
    "WHEN uncertain_count > retained_submission_count * 0.6 THEN 'VUS'",
    "WHEN pathogenic_count > 0 THEN 'Pathogenic/Likely Pathogenic'",
    "WHEN benign_count > 0 THEN 'Benign' ELSE 'VUS' END AS policy_classification",
    "FROM summarized)",
    "SELECT policy_version, profile_id, release_id, vcv_accession, variation_id, allele_id,",
    "policy_classification, gold_stars, modern_filter_applied, eligible_submission_count,",
    "retained_submission_count, retained_scv_count, retained_submitter_count,",
    "pathogenic_count, benign_count, uncertain_count, latest_date_last_evaluated FROM decided"
  )
}
