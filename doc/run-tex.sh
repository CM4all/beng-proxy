#!/bin/sh -e
set -x

if test -z "$1" -o -n "$3"; then
    echo "Usage: $0 FILE.tex [OUTDIR]" >&2
    exit 1
fi

if test -n "$2"; then
    OUTDIR="$2"
else
    OUTDIR="."
fi

TEX_FLAGS="--interaction=nonstopmode --no-shell-escape --output-format=pdf"
TEX_RUN="lualatex ${TEX_FLAGS} --output-directory=${OUTDIR}"

${TEX_RUN} "$1"
${TEX_RUN} "$1"
