#!/bin/bash

# buildhtml.sh

# Builds the html static site, using mkdocs
# Assumes that the script is executed from the root netdata folder, by calling htmldoc/buildhtml.sh

# Copy all netdata .md files to htmldoc/src. Exclude htmldoc itself and also the directory node_modules generated by Netlify
echo "Copying files"
rm -rf htmldoc/src
find . -type d \( -path ./htmldoc -o -path ./node_modules \) -prune -o -name "*.md" -print | cpio -pd htmldoc/src

# Modify the first line of the main README.md, to enable proper static html generation 
sed -i '0,/# netdata /s//# Introducing NetData\n\n/' htmldoc/src/README.md

echo "Creating mkdocs.yaml"

# Generate mkdocs.yaml
htmldoc/buildyaml.sh > htmldoc/mkdocs.yml

echo "Fixing links"

# Fix links (recursively, all types, executing replacements)
htmldoc/checklinks.sh -rax
if [ $? -eq 1 ] ; then exit 1 ; fi

echo "Calling mkdocs"

# Build html docs
mkdocs build --config-file=htmldoc/mkdocs.yml

echo "Finished"

