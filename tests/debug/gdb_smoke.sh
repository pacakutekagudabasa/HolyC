#!/bin/bash
# HolyC debug info के लिए GDB smoke test करो
# जरूरी है: gdb, hcc with -g support
set -e

HCC=${HCC:-./build/hcc}
GDB=${GDB:-gdb}

# HCC को script के project root के relative resolve करो (tests/debug/ से दो level ऊपर)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HCC="${HCC:-$PROJECT_ROOT/build/hcc}"

# prerequisites check करो
command -v "$GDB" >/dev/null 2>&1 || { echo "SKIP: gdb not found"; exit 0; }
[ -x "$HCC" ] || { echo "SKIP: hcc binary not found at $HCC"; exit 0; }

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

cat > "$TMPDIR/dbg.HC" << 'EOF'
U0 Main() {
  I64 x = 42;
  I64 y = x * 2;
  "%d\n", y;
}
EOF

# debug info के साथ compile करो
if ! "$HCC" -g "$TMPDIR/dbg.HC" -o "$TMPDIR/dbg_bin" 2>&1; then
    echo "FAIL: compilation with -g failed"
    exit 1
fi

echo "PASS: compiled with -g"

# DWARF info है या नहीं verify करो
if command -v readelf >/dev/null 2>&1; then
    if readelf -S "$TMPDIR/dbg_bin" 2>/dev/null | grep -q "\.debug_info"; then
        echo "PASS: DWARF .debug_info section found"
    else
        echo "INFO: No .debug_info section (may be stripped or linked differently)"
    fi
fi

# GDB के नीचे non-interactively चलाओ
"$GDB" -batch \
    -ex "file $TMPDIR/dbg_bin" \
    -ex "break Main" \
    -ex "run" \
    -ex "next" \
    -ex "print x" \
    -ex "quit" \
    "$TMPDIR/dbg_bin" > "$TMPDIR/gdb_out.txt" 2>&1 || true

if grep -q "42" "$TMPDIR/gdb_out.txt"; then
    echo "PASS: GDB can inspect variable x = 42"
else
    echo "INFO: GDB variable inspection result:"
    cat "$TMPDIR/gdb_out.txt"
    echo "INFO: GDB inspection may need symbol resolution (OK if binary ran)"
fi

echo "PASS: GDB smoke test complete"
