# Phase-2 SQL inventory

Classification: **A** = read-only relational SQL over the extension or
ClinVar/PubMed tables, a candidate for a client-neutral table macro (after
specifying parameters and prerequisites); **B** = schema ownership, import,
file I/O, connection or DuckLake publication, kept in the R front end. This
is an inventory, not a proposal to move persistent views or tables now.

| Source | Class | SQL relations, macros or generated relation | Boundary |
| --- | --- | --- | --- |
| `schema.R` | B | `clinvar_releases`, `clinvar`, `clinvar_policy_profiles`, `clinvar_policy_submitter_exclusions`; `pubmed_sources`, `pubmed_articles`, `pubmed_abstracts`, `pubmed_article_identifiers`, `pubmed_mesh_terms`, `pubmed_keywords` | Persistent tables, initialization and source identity |
| `schema.R` | A | `clinvar_variants`, `clinvar_alleles`, `clinvar_locations`, `clinvar_genes`, `clinvar_rcv_assertions`, `clinvar_scv_assertions`, `clinvar_conditions`, `clinvar_condition_names`, `clinvar_xrefs`, `clinvar_observations`, `clinvar_citations`, `clinvar_citation_identifiers`, `clinvar_attributes`, `clinvar_text`, `clinvar_vcf`, `clinvar_normalized_alleles`, `clinvar_disease_aggregates`, `clinvar_disease_submissions`, `clinvar_hpo_terms`, `clinvar_literature_links`, `clinvar_semantic_documents` | Views over the canonical `clinvar` table |
| `schema.R` | A | `pubmed_article_events`, `pubmed_literature_snapshots`, `pubmed_literature_article_versions`, `pubmed_literature_sections`, `pubmed_current_articles`, `pubmed_current_abstracts`, `pubmed_current_article_identifiers`, `pubmed_current_mesh_terms`, `pubmed_current_keywords`, `clinvar_pubmed_articles`, `clinvar_gene_summaries`, `clinvar_gene_disease_summaries` | Read-only joins and snapshot views |
| `schema.R` | A | `pubmed_articles_as_of`, `pubmed_abstracts_as_of`, `pubmed_article_identifiers_as_of`, `pubmed_mesh_terms_as_of`, `pubmed_keywords_as_of` | Parameterized table macros |
| `policy.R` | A | `clinvar_policy_decisions`, `clinvar_policy_pathogenic_alleles`, `clinvar_policy_allele_decisions`; `rclinvarbitration_allele_policy_query()` | Relational classification; requires explicit pinned policy version and profile tables |
| `schema.R`, `pubmed.R` | B | `rclinvarbitration_import_entities`, `rclinvarbitration_pubmed_import_rows` (temporary staging); `rclinvarbitration_import_statements()` and PubMed INSERT statements | Import transaction, source labels, parser input and persisted writes |
| `transition.R` | A | `rclinvarbitration_disease_release_transitions()` result SELECT | Parameterized old/new release and profile relation; R validates imports and returns rows |
| `flat.R` | A | `rclinvarbitration_flat_tidy_sql()` SELECT | Relational projection of supplied flat-file tables, parameterized by source paths and policy |
| `flat.R` | B | `rclinvarbitration_import_flat()` INSERT/COPY and release registration | Files, identity and transaction |
| `reproduce.R` | A | `rclinvarbitration_reproduction_sql()` SELECT | Classification query over supplied TSV paths |
| `reproduce.R` | B | `rclinvarbitration_reproduce_clinvarbitration_parquet()` COPY | Writes Parquet |
| `export.R` | A | `rclinvarbitration_compatibility_select_sql()`, `rclinvarbitration_tidy_select_sql()` | Parameterized relational exports over ClinVar/policy relations |
| `export.R` | B | `rclinvarbitration_export_tidy_parquet()`, `rclinvarbitration_export_clinvarbitration_parquet()` COPY | Profile validation and file output |
| `ducklake.R` | B | Publisher target/staging CREATE/INSERT, DESCRIBE/SELECT over `read_parquet` | Catalog, paths and publication transaction |
| `download.R`, `connection.R` | B | Source fetch and extension loading | Network, filesystem and connection lifecycle |

The native table functions `clinvar_xml_entities` and
`rclinvarbitration_pubmed_xml_rows`, and scalar `rclinvar_json_field`, are
already extension APIs, not R-defined relations.
