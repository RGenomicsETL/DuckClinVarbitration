# ClinVarbitration policy SQL

Returns DuckDB views implementing the supported policy over
`clinvar_disease_submissions`. The policy bins submitted
classifications, excludes unknown bins and applies the upstream policy's
intended qualified Illumina benign exclusion, optionally applies
profile-specific submitter exclusions, prefers evidence evaluated from
2016 onward while always retaining expert-panel and practice guideline
evidence, and applies the 60/20 majority rule. The disease-level and
allele-level views both reproduce the upstream strong-review rule: the
first retained practice-guideline or expert-panel classification in
source order is decisive.

## Usage

``` r
rclinvarbitration_policy_sql(
  policy_version = rclinvarbitration_policy_version()
)
```

## Arguments

- policy_version:

  Exact supported policy version. See
  [`rclinvarbitration_policy_version()`](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/reference/rclinvarbitration_policy_version.md).

## Value

A named character vector of DuckDB `CREATE OR REPLACE VIEW` statements.

## Details

`clinvar_policy_pathogenic_alleles` is the disease-specific pathogenic
or likely-pathogenic join surface for downstream Rduckhts/DuckHTS
annotation. It does not perform VEP or PM5 computation.
