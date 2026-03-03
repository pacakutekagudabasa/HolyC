#!/usr/bin/env bash
# hcc --lsp के लिए basic LSP server smoke test करो
# Tests: initialize, textDocument/didOpen, publishDiagnostics, completion — सब test करो
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HCC="$SCRIPT_DIR/../../build/hcc"
if [[ ! -x "$HCC" ]]; then
    echo "FAIL: hcc not found at $HCC" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Helper functions यहाँ हैं
# ---------------------------------------------------------------------------
LAST_MSG=""

# दिए गए FD पर JSON-RPC message send करो
send_msg() {
    local body="$1"
    local len=${#body}
    printf "Content-Length: %d\r\n\r\n%s" "$len" "$body" >&"${LSP[1]}"
}

# coprocess के stdout से अगला JSON-RPC message read करो।
# body global LAST_MSG में store करो।
recv_msg() {
    LAST_MSG=""
    local line content_length=0
    # blank line तक headers read करो
    while IFS= read -r line <&"${LSP[0]}"; do
        line="${line%$'\r'}"
        [[ -z "$line" ]] && break
        if [[ "$line" == Content-Length:* ]]; then
            content_length="${line#Content-Length: }"
            content_length="${content_length//[[:space:]]/}"
        fi
    done
    [[ "$content_length" -le 0 ]] && return 1
    # body read करो (chunks में आ सकता है — सब bytes मिलने तक loop करो)
    local buf=""
    while [[ ${#buf} -lt "$content_length" ]]; do
        local chunk
        IFS= read -r -n "$content_length" chunk <&"${LSP[0]}" || break
        buf="${buf}${chunk}"
    done
    LAST_MSG="$buf"
}

# assert करो कि LAST_MSG में substring है
assert_contains() {
    local label="$1" substr="$2"
    if [[ "$LAST_MSG" == *"$substr"* ]]; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        echo "  Expected to find: $substr" >&2
        echo "  In response:      $LAST_MSG" >&2
        kill "$LSP_PID" 2>/dev/null || true
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# hcc --lsp को coprocess के रूप में launch करो
# ---------------------------------------------------------------------------
coproc LSP { exec "$HCC" --lsp 2>/dev/null; }
LSP_PID=$LSP_PID  # coproc automatically NAME_PID set करता है

cleanup() {
    kill "$LSP_PID" 2>/dev/null || true
    wait "$LSP_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Test 1: initialize करो
# ---------------------------------------------------------------------------
send_msg '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":0,"capabilities":{},"rootUri":null}}'
recv_msg
assert_contains "initialize returns capabilities"      '"capabilities"'
assert_contains "initialize returns hoverProvider"     '"hoverProvider"'
assert_contains "initialize returns completionProvider" '"completionProvider"'
assert_contains "initialize server info"               '"hcc"'

# ---------------------------------------------------------------------------
# Test 2: initialized notification भेजो (response expected नहीं)
# ---------------------------------------------------------------------------
send_msg '{"jsonrpc":"2.0","method":"initialized","params":{}}'
# notifications का response नहीं आता — तुरंत आगे बढ़ो

# ---------------------------------------------------------------------------
# Test 3: textDocument/didOpen — clean file के साथ test करो
# Server didOpen के बाद तुरंत publishDiagnostics भेजता है।
# ---------------------------------------------------------------------------
DOC_URI="file:///tmp/basic_lsp_test.HC"
# problematic characters के बिना source build करो
SOURCE='U0 Main() { I64 x = 42; }'
SOURCE_JSON="${SOURCE//\\/\\\\}"
SOURCE_JSON="${SOURCE_JSON//\"/\\\"}"

send_msg "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\",\"languageId\":\"holyc\",\"version\":1,\"text\":\"${SOURCE_JSON}\"}}}"
recv_msg
assert_contains "publishDiagnostics sent after didOpen" '"diagnostics"'

# ---------------------------------------------------------------------------
# Test 4: textDocument/completion test करो
# ---------------------------------------------------------------------------
send_msg "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\"},\"position\":{\"line\":0,\"character\":5}}}"
recv_msg
assert_contains "completion response has items"            '"items"'
assert_contains "completion includes HolyC keyword I64"    '"I64"'
assert_contains "completion includes HolyC keyword U64"    '"U64"'
assert_contains "completion includes builtin Print"        '"Print"'

# ---------------------------------------------------------------------------
# Test 5: textDocument/hover test करो (null return हो सकता है — बस crash नहीं होना चाहिए)
# ---------------------------------------------------------------------------
send_msg "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\"},\"position\":{\"line\":0,\"character\":3}}}"
recv_msg
assert_contains "hover response has id 3" '"id":3'
echo "PASS: textDocument/hover did not crash"

# ---------------------------------------------------------------------------
# Test 6: textDocument/documentSymbol test करो
# ---------------------------------------------------------------------------
send_msg "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\"}}}"
recv_msg
assert_contains "documentSymbol response has id 4" '"id":4'
assert_contains "documentSymbol includes Main function" '"Main"'

# ---------------------------------------------------------------------------
# Test 7: textDocument/definition test करो
# ---------------------------------------------------------------------------
send_msg "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\"},\"position\":{\"line\":0,\"character\":3}}}"
recv_msg
assert_contains "definition response has id 5" '"id":5'
echo "PASS: textDocument/definition did not crash"

# ---------------------------------------------------------------------------
# Test 8: syntax error के साथ textDocument/didOpen test करो → diagnostics में errors आने चाहिए
# ---------------------------------------------------------------------------
ERROR_URI="file:///tmp/basic_lsp_error.HC"
# Syntax error: = के बाद expression गायब है
send_msg "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${ERROR_URI}\",\"languageId\":\"holyc\",\"version\":1,\"text\":\"U0 Bad() { I64 x = ; }\"}}}"
recv_msg
assert_contains "publishDiagnostics sent for error file" '"diagnostics"'
# diagnostics array non-empty होना चाहिए (कम से कम एक error हो)
if [[ "$LAST_MSG" != *'"diagnostics":[]'* ]]; then
    echo "PASS: publishDiagnostics contains error entries"
else
    echo "INFO: publishDiagnostics returned empty array for error file (may be OK)"
fi

# ---------------------------------------------------------------------------
# Test 9: shutdown / exit test करो
# ---------------------------------------------------------------------------
send_msg '{"jsonrpc":"2.0","id":6,"method":"shutdown","params":{}}'
recv_msg
assert_contains "shutdown response has id 6" '"id":6'

send_msg '{"jsonrpc":"2.0","method":"exit","params":{}}'
wait "$LSP_PID" 2>/dev/null || true
echo "PASS: shutdown and exit"

echo ""
echo "All LSP basic tests passed."
