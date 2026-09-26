# duckclinvarbitration

DuckDB C extension for streaming ClinVar VCV XML and PubMed XML into SQL.
With a binary built for your exact DuckDB version (`make`), start DuckDB with
`-unsigned` and run:

```sql
LOAD './build/release/duckclinvarbitration.duckdb_extension';
SELECT entity_type, count(*) AS n
FROM clinvar_xml_entities('r/RClinVarbitration/inst/extdata/VCV_XML_VCV000091629.xml.gz')
GROUP BY entity_type;

SELECT pmid, article_title
FROM rclinvarbitration_pubmed_xml_rows('r/RClinVarbitration/inst/extdata/pubmed_baseline_fixture.xml')
WHERE entity_type = 'article';
```

The binary uses statically linked libxml2 and zlib; build with a vcpkg toolchain
providing static PIC archives. `make test` runs SQL logic tests and checks
exported symbols. The C ABI is pinned to the exact DuckDB release.

## R front end

The R package **RClinVarbitration** lives in [`r/RClinVarbitration/`](r/RClinVarbitration/).
From that directory, run `Rscript bootstrap.R ../..` before building the package.
Its R API, ClinVar policy, publication logic and host-library build remain
separate from the standalone extension.
