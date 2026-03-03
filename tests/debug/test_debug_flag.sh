#!/bin/bash
# Test करो कि -g और --debug flags compilation तोड़े बिना काम करते हैं

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HCC="${HCC:-$PROJECT_ROOT/build/hcc}"

[ -x "$HCC" ] || { echo "SKIP: hcc binary not found at $HCC"; exit 0; }

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

cat > "$TMPDIR/dbg.HC" << 'EOF'
U0 Main() { I64 x = 99; "%d\n", x; }
EOF

# -g flag test करो
out=$("$HCC" -g "$TMPDIR/dbg.HC" -o "$TMPDIR/dbg_bin" && "$TMPDIR/dbg_bin" 2>/dev/null)
if [ "$out" = "99" ]; then
    echo "PASS: -g flag works"
else
    echo "FAIL: -g got '$out'"
    exit 1
fi

# --debug flag test करो
out2=$("$HCC" --debug "$TMPDIR/dbg.HC" -o "$TMPDIR/dbg_bin2" && "$TMPDIR/dbg_bin2" 2>/dev/null)
if [ "$out2" = "99" ]; then
    echo "PASS: --debug flag works"
else
    echo "FAIL: --debug got '$out2'"
    exit 1
fi

echo "PASS: all debug flag tests passed"
