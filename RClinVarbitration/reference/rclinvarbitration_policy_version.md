# Current ClinVarbitration policy version

The identifier pins the policy semantics adapted from Centre for
Population Genomics ClinVarbitration 2.2.11 (upstream commit
`658b9f241eb2d43aa11214b153b19c1e18a16337`) and records that decisions
are grouped per disease rather than only per variation. The identifier
has no package-local suffix: the view names, rather than a speculative
`v1`, state whether the output is disease-scoped or allele-scoped.

## Usage

``` r
rclinvarbitration_policy_version()
```

## Value

A character scalar.
