#!/usr/bin/env Rscript

rmarkdown::render("README.Rmd", output_file = "README.md", quiet = TRUE)
markdown <- readLines("README.md", warn = FALSE, encoding = "UTF-8")
writeLines(sub("^``` sql$", "```sql", markdown), "README.md", useBytes = TRUE)
