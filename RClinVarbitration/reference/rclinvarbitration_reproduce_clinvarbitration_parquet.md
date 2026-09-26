# Reproduce ClinVarbitration decisions from archived ClinVar flat files

Reproduces Centre for Population Genomics ClinVarbitration 2.2.11's
allele-level decision algorithm directly from NCBI's versioned
`submission_summary` and `variant_summary` files. The generated Parquet
has the exact seven-column `clinvar_decisions.tsv` layout: `contig`,
`position`, `reference`, `alternate`, `clinical_significance`,
`gold_stars`, and `allele_id`.

## Usage

``` r
rclinvarbitration_reproduce_clinvarbitration_parquet(
  con,
  submission_path,
  variant_path,
  path,
  assembly = c("GRCh38", "GRCh37"),
  submitter_exclusions = character()
)
```

## Arguments

- con:

  A DuckDB DBI connection.

- submission_path:

  Archived NCBI `submission_summary_YYYY-MM.txt.gz`.

- variant_path:

  Archived NCBI `variant_summary_YYYY-MM.txt.gz`.

- path:

  New output `.parquet` path.

- assembly:

  Genome assembly: `"GRCh38"` or `"GRCh37"`.

- submitter_exclusions:

  Submitter names to exclude for a blinded run.

## Value

A named list describing the written Parquet file, invisibly.

## Details

This is separate from
[`rclinvarbitration_import_xml()`](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/reference/rclinvarbitration_import_xml.md)
and the disease-aware policy views. It exists to reproduce and
differentially validate the upstream allele-level artifact on matching
archived flat-file releases. It uses DuckDB's streaming CSV reader and
SQL aggregation; it does not invoke Python, Hail, VEP, or PM5 logic.
