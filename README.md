
<!-- README.md is generated from README.Rmd using [duckknit](https://github.com/rundel/duckknit). Please edit that file. -->

# DuckClinVarbitration

[![Extension CI](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/extension.yml/badge.svg)](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/extension.yml)
[![Site](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/pkgdown.yaml/badge.svg)](https://rgenomicsetl.github.io/DuckClinVarbitration/)
[![r-universe](https://rgenomicsetl.r-universe.dev/badges/RClinVarbitration)](https://rgenomicsetl.r-universe.dev/RClinVarbitration)

ClinVar is a ledger of submissions, and they often disagree. One variant can carry a decades-old
consortium record, a handful of clinical-laboratory submissions and an expert
panel review, each with its own classification, criteria and date. ClinVar’s
aggregate label is one summary of that ledger;
[ClinVarbitration](https://github.com/populationgenomics/clinvarbitration),
from the Centre for Population Genomics, is another: an explicit policy that
bins classifications, keeps modern evidence, lets expert panels decide and
otherwise requires a 60/20 majority.

DuckClinVarbitration runs that policy inside DuckDB. A small C extension streams
ClinVar VCV XML and PubMed XML into relations any DuckDB client can query; the R
front end, [RClinVarbitration](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/),
imports complete releases, applies the pinned policy and publishes versioned
results. Every decision stays joined to the submissions that produced it.

## What it does at release scale

| Measure                                         | Result                                                                                                                                                                                      |
|:------------------------------------------------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Agreement with upstream ClinVarbitration 2.2.11 | **4,125,389** GRCh38 alleles, **0** classification or star differences ([oracle](r/RClinVarbitration/inst/audits/march-2026-flat-exact-oracle.dcf))                                         |
| Full VCV XML release, ncbi-vcv-2026-07-02       | 5.8 GB gzipped XML to **109,372,736** scalar facts in 28.5 min, 4 threads, 0 duplicate keys ([receipt](r/RClinVarbitration/inst/benchmarks/full-release-2026-07-02.dcf))                    |
| Flat summary reports, ncbi-flat-2026-03         | **38,596,056** rows, 4,124,600 allele decisions, in 3.7 min ([receipt](r/RClinVarbitration/inst/benchmarks/full-flat-release-2026-03.dcf))                                                  |
| XML against flat decisions                      | 4,125,382 shared alleles; all 377 differences classified with source-row receipts ([audit](r/RClinVarbitration/inst/audits/march-2026-xml-flat-differential.dcf), [errata](docs/ERRATA.md)) |

Upstream runs on Python, Hail, Nextflow and bcftools. Here it is one DuckDB
database and a C extension built on libxml2’s streaming reader.

## One variant, from submissions to decision

The repository’s fixture is VCV000091629, a splice-donor variant in *BRCA1*.
The extension exposes each submission as a row, from the DuckDB CLI or any other
client:

```sql
SELECT rclinvar_json_field(fields_json, 'scv_accession')       AS scv,
       rclinvar_json_field(fields_json, 'submitter_name')      AS submitter,
       rclinvar_json_field(fields_json, 'classification')      AS classification,
       rclinvar_json_field(fields_json, 'review_status')       AS review_status,
       rclinvar_json_field(fields_json, 'date_last_evaluated') AS evaluated
FROM clinvar_xml_entities('r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz')
WHERE entity_type = 'scv_assertion'
ORDER BY evaluated;
```

    ┌──────────────┬──────────────────────────────────────────────┬───────────────────┬─────────────────────────────────────┬────────────┐
    │     scv      │                  submitter                   │  classification   │            review_status            │ evaluated  │
    │   varchar    │                   varchar                    │      varchar      │               varchar               │  varchar   │
    ├──────────────┼──────────────────────────────────────────────┼───────────────────┼─────────────────────────────────────┼────────────┤
    │ SCV000145071 │ Breast Cancer Information Core (BIC) (BRCA1) │ Pathogenic        │ no assertion criteria provided      │ 1999-06-22 │
    │ SCV000108943 │ Sharing Clinical Reports Project (SCRP)      │ Pathogenic        │ no assertion criteria provided      │ 2012-09-24 │
    │ SCV000569301 │ GeneDx                                       │ Pathogenic        │ criteria provided, single submitter │ 2016-08-16 │
    │ SCV000688489 │ Color Diagnostics, LLC DBA Color Health      │ Likely pathogenic │ criteria provided, single submitter │ 2022-02-09 │
    │ SCV000827729 │ Labcorp Genetics (formerly Invitae), Labcorp │ Pathogenic        │ criteria provided, single submitter │ 2022-10-28 │
    │ SCV003995313 │ Ambry Genetics                               │ Pathogenic        │ criteria provided, single submitter │ 2023-05-19 │
    └──────────────┴──────────────────────────────────────────────┴───────────────────┴─────────────────────────────────────┴────────────┘

Two submissions predate 2016 and carry no assertion criteria. The policy keeps
only modern evidence when any exists, so four submissions are left, all
pathogenic or likely pathogenic. The R front end imports the same file and
reports the decision together with the counts behind it:

``` r
library(DBI)
library(RClinVarbitration)

con <- dbConnect(duckdb::duckdb(config = list(allow_unsigned_extensions = "true")))
rclinvarbitration_enable(con)
rclinvarbitration_init(con)
invisible(rclinvarbitration_import_xml(
  con, "r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz",
  release_id = "fixture"
))

dbGetQuery(con, "
  SELECT policy_classification, gold_stars, modern_filter_applied,
         eligible_submission_count AS eligible,
         retained_submission_count AS retained,
         pathogenic_count AS P, benign_count AS B, uncertain_count AS U
  FROM clinvar_policy_allele_decisions
")
#>          policy_classification gold_stars modern_filter_applied eligible
#> 1 Pathogenic/Likely Pathogenic          1                  TRUE        6
#>   retained P B U
#> 1        4 4 0 0
```

`clinvar_policy_decisions` gives the same answer per disease, and
`rclinvarbitration_disease_release_transitions()` lists the alleles whose
decision changed between two releases. The [algorithm
article](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/articles/arbitration-algorithm.html)
walks through every rule, including where this package follows upstream’s
documented intent rather than its code.

## Literature with history

PubMed ships a baseline and then daily updates that revise and delete records.
The scanner keeps each version as a row; RClinVarbitration stacks them into
`pubmed_*_as_of(source)` views, so an analysis can ask what the literature
looked like at any source cutoff.

```sql
SELECT 'baseline' AS source, pmid, article_title, is_deleted
FROM rclinvarbitration_pubmed_xml_rows('r/RClinVarbitration/inst/extdata/pubmed_baseline_fixture.xml')
WHERE entity_type = 'article'
UNION ALL
SELECT 'update', pmid, article_title, is_deleted
FROM rclinvarbitration_pubmed_xml_rows('r/RClinVarbitration/inst/extdata/pubmed_update_fixture.xml')
WHERE entity_type = 'article'
UNION ALL
SELECT 'delete', pmid, article_title, is_deleted
FROM rclinvarbitration_pubmed_xml_rows('r/RClinVarbitration/inst/extdata/pubmed_delete_fixture.xml')
WHERE entity_type = 'article';
```

    ┌──────────┬─────────┬────────────────────────┬────────────┐
    │  source  │  pmid   │     article_title      │ is_deleted │
    │ varchar  │ varchar │        varchar         │  boolean   │
    ├──────────┼─────────┼────────────────────────┼────────────┤
    │ baseline │ 1001    │ Baseline article title │ false      │
    │ update   │ 1001    │ Updated article title  │ false      │
    │ delete   │ 1001    │ NULL                   │ true       │
    │ delete   │ 1002    │ NULL                   │ true       │
    └──────────┴─────────┴────────────────────────┴────────────┘

## Where things live

|                                                                                                                      | Owns                                                                                                                                              |
|:---------------------------------------------------------------------------------------------------------------------|:--------------------------------------------------------------------------------------------------------------------------------------------------|
| `duckclinvarbitration` extension (`src/`)                                                                            | `clinvar_xml_entities(path)`, `rclinvarbitration_pubmed_xml_rows(path)`, `rclinvar_json_field(json, key)`: forward XML scanning, nothing else     |
| [RClinVarbitration](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/) (`r/RClinVarbitration/`) | release download and import, the pinned policy, release transitions, Parquet and [DuckLake](https://ducklake.select/) publication, PubMed history |
| [ducksemantics](https://github.com/RGenomicsETL/ducksemantics)                                                       | retrieval and grounding over the evidence and literature relations                                                                                |

Known deviations from upstream and from ClinVar itself are recorded, with
receipts, in the [errata](docs/ERRATA.md).

## Build

``` sh
git clone --recurse-submodules https://github.com/RGenomicsETL/DuckClinVarbitration
cd DuckClinVarbitration
make configure release test
duckdb -unsigned -c "LOAD 'build/release/duckclinvarbitration.duckdb_extension'"
```

The DuckDB CLI must match the extension’s pinned DuckDB version. `make readme`
renders this file with the official CLI in `build/tools/duckdb` (override with
`DUCKDB_CLI`) and the R package installed from `r/RClinVarbitration`.

## License

GPL-2.0-or-later; see [LICENSE](LICENSE). The R front end declares the same licence as `GPL (>= 2)`.

## Acknowledgements

The decision policy is adapted from the Centre for Population Genomics’
[ClinVarbitration](https://github.com/populationgenomics/clinvarbitration)
under its MIT licence. ClinVar and PubMed data are provided by NCBI.
