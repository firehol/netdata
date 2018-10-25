#!/usr/bin/env sh

cat src/prologue.js.inc \
    src/utils.js \
    src/compatibility.js \
    src/xss.js \
    src/colors.js \
    src/units-conversion.js \
    src/sparkline.js \
    src/main.js \
    src/epilogue.js.inc \
    > dashboard.js
