#!/bin/bash
# Rebuild ports/host_wasm/web/vendor/codemirror.js from the pinned CodeMirror 6
# packages.  Run after bumping versions; otherwise the bundle is
# checked in and no rebuild is needed.
#
# Uses a scratch directory under /tmp so no node_modules lands in
# this tree.  esbuild is installed locally to that scratch dir.

set -e
here="$(cd "$(dirname "$0")" && pwd -P)"
work="$(mktemp -d /tmp/cm-build.XXXXXX)"
trap 'rm -rf "$work"' EXIT

cat > "$work/package.json" <<'JSON'
{"name":"cm-build","private":true,"type":"module"}
JSON

cat > "$work/entry.js" <<'JS'
export { EditorView, basicSetup } from 'codemirror';
export { StreamLanguage }         from '@codemirror/language';
export { vb as basic }            from '@codemirror/legacy-modes/mode/vb';
export { oneDark }                from '@codemirror/theme-one-dark';
JS

cd "$work"
npm install --silent --no-fund --no-audit \
    codemirror@6.0.1 \
    @codemirror/language@6.10.2 \
    @codemirror/legacy-modes@6.4.1 \
    @codemirror/theme-one-dark@6.1.2 \
    esbuild@0.23.0

./node_modules/.bin/esbuild entry.js \
    --bundle --format=esm --minify \
    --outfile="$here/codemirror.js"

echo "Wrote $here/codemirror.js ($(wc -c < "$here/codemirror.js") bytes)"
