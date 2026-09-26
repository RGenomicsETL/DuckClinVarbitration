
<!-- README.md is generated from README.Rmd using [duckknit](https://github.com/rundel/duckknit). Please edit that file. -->

# DuckClinVarbitration

[![Extension
CI](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/extension.yml/badge.svg)](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/extension.yml)
[![Site](https://github.com/RGenomicsETL/DuckClinVarbitration/actions/workflows/pkgdown.yaml/badge.svg)](https://rgenomicsetl.github.io/DuckClinVarbitration/)

DuckDB C extension for streaming ClinVar VCV XML and PubMed XML into
SQL. Build with `make release` and open a DuckDB CLI matching the
extension’s pinned DuckDB version with `-unsigned`. For README
generation, download the matching official CLI (Linux amd64:
[`v1.5.2/duckdb_cli-linux-amd64.zip`](https://github.com/duckdb/duckdb/releases/download/v1.5.2/duckdb_cli-linux-amd64.zip))
into the ignored `build/tools/` directory, then run `make readme`. Set
`DUCKDB_CLI=/path/to/duckdb` if the binary is elsewhere. The queries
below run against repository fixtures used by the [SQL logic
tests](https://github.com/RGenomicsETL/DuckClinVarbitration/tree/main/test/sql).

## SQL reference

- `clinvar_xml_entities(path)` streams VCV variation, RCV and SCV
  entities with stable identifiers and JSON fields.
- `rclinvarbitration_pubmed_xml_rows(path)` streams PMID-linked article,
  abstract and other PubMed entities.
- `rclinvar_json_field(json, key)` extracts a field from an entity’s
  JSON text.

### ClinVar entity shape

```sql
SELECT vcv_accession, entity_type, entity_id, parent_type
FROM clinvar_xml_entities('r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz')
WHERE entity_type = 'rcv_assertion'
ORDER BY entity_id
LIMIT 2;
```

    ┌───────────────┬───────────────┬──────────────┬─────────────┐
    │ vcv_accession │  entity_type  │  entity_id   │ parent_type │
    │    varchar    │    varchar    │   varchar    │   varchar   │
    ├───────────────┼───────────────┼──────────────┼─────────────┤
    │ VCV000091629  │ rcv_assertion │ RCV000077146 │ variation   │
    │ VCV000091629  │ rcv_assertion │ RCV000483230 │ variation   │
    └───────────────┴───────────────┴──────────────┴─────────────┘

### Assertions by entity type

These fixture counts expose the VCV’s variation, condition-level RCV and
submission-level SCV structure; arbitration policy belongs to the [R
package](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/).

```sql
SELECT entity_type, count(*) AS assertions
FROM clinvar_xml_entities('r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz')
WHERE entity_type IN ('variation', 'rcv_assertion', 'scv_assertion')
GROUP BY entity_type
ORDER BY entity_type;
```

    ┌───────────────┬────────────┐
    │  entity_type  │ assertions │
    │    varchar    │   int64    │
    ├───────────────┼────────────┤
    │ rcv_assertion │          4 │
    │ scv_assertion │          6 │
    │ variation     │          1 │
    └───────────────┴────────────┘

### Condition-level classifications

RCV classifications and review status are source evidence for
arbitration, not an arbitration decision.

```sql
SELECT entity_id AS rcv,
       rclinvar_json_field(fields_json, 'classification') AS classification,
       rclinvar_json_field(fields_json, 'review_status') AS review_status
FROM clinvar_xml_entities('r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz')
WHERE entity_type = 'rcv_assertion'
ORDER BY entity_id
LIMIT 3;
```

    ┌──────────────┬──────────────────────────────┬──────────────────────────────────────────────────────┐
    │     rcv      │        classification        │                    review_status                     │
    │   varchar    │           varchar            │                       varchar                        │
    ├──────────────┼──────────────────────────────┼──────────────────────────────────────────────────────┤
    │ RCV000077146 │ Pathogenic                   │ no assertion criteria provided                       │
    │ RCV000483230 │ Pathogenic                   │ criteria provided, single submitter                  │
    │ RCV000581212 │ Pathogenic/Likely pathogenic │ criteria provided, multiple submitters, no conflicts │
    └──────────────┴──────────────────────────────┴──────────────────────────────────────────────────────┘

### PubMed entity shape and article

```sql
SELECT pmid, entity_type, article_title, is_deleted
FROM rclinvarbitration_pubmed_xml_rows('r/RClinVarbitration/inst/extdata/pubmed_baseline_fixture.xml')
WHERE entity_type = 'article';
```

    ┌─────────┬─────────────┬────────────────────────┬────────────┐
    │  pmid   │ entity_type │     article_title      │ is_deleted │
    │ varchar │   varchar   │        varchar         │  boolean   │
    ├─────────┼─────────────┼────────────────────────┼────────────┤
    │ 1001    │ article     │ Baseline article title │ false      │
    └─────────┴─────────────┴────────────────────────┴────────────┘

See the [R package
site](https://rgenomicsetl.github.io/DuckClinVarbitration/RClinVarbitration/)
for arbitration and import APIs, [project
documentation](https://rgenomicsetl.github.io/DuckClinVarbitration/docs/)
for errata, and the [GitHub
repository](https://github.com/RGenomicsETL/DuckClinVarbitration) for
source.
