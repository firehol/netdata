#!/bin/bash

# buildhtml.sh

# Builds the html static site, using mkdocs
# Assumes that the script is executed either from the htmldoc folder (by netlify), or from the root repo dir (as originally intended) 
currentdir=$(pwd | awk -F '/' '{print $NF}')
if [ $currentdir = "htmldoc" ] ; then 
	cd ..
fi


# Copy all netdata .md files to htmldoc/src. Exclude htmldoc itself and also the directory node_modules generated by Netlify
echo "Copying files"
rm -rf htmldoc/src
find . -type d \( -path ./htmldoc -o -path ./node_modules \) -prune -o -name "*.md" -print | cpio -pd htmldoc/src

# Modify the first line of the main README.md, to enable proper static html generation 
sed -i '0,/# netdata /s//# Introduction\n\n/' htmldoc/src/README.md

# Remove specific files that don't belong in the documentation
rm htmldoc/src/HISTORICAL_CHANGELOG.md
rm htmldoc/src/collectors/charts.d.plugin/mem_apps/README.md
rm htmldoc/src/collectors/charts.d.plugin/postfix/README.md
rm htmldoc/src/collectors/charts.d.plugin/tomcat/README.md
rm htmldoc/src/collectors/charts.d.plugin/sensors/README.md
rm htmldoc/src/collectors/charts.d.plugin/cpu_apps/README.md
rm htmldoc/src/collectors/charts.d.plugin/squid/README.md
rm htmldoc/src/collectors/charts.d.plugin/nginx/README.md
rm htmldoc/src/collectors/charts.d.plugin/hddtemp/README.md
rm htmldoc/src/collectors/charts.d.plugin/cpufreq/README.md
rm htmldoc/src/collectors/charts.d.plugin/mysql/README.md
rm htmldoc/src/collectors/charts.d.plugin/exim/README.md
rm htmldoc/src/collectors/charts.d.plugin/apache/README.md
rm htmldoc/src/collectors/charts.d.plugin/load_average/README.md
rm htmldoc/src/collectors/charts.d.plugin/phpfpm/README.md

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

