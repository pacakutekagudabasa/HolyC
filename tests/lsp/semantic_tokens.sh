#!/usr/bin/env bash
# LSP semantic tokens smoke test करो
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HCC="$SCRIPT_DIR/../../build/hcc"
if [[ ! -x "$HCC" ]]; then
    echo "FAIL: hcc not found at $HCC" >&2
    exit 1
fi

LAST_MSG=""

send_msg() {
    local body="$1"
    local len=${#body}
    printf "Content-Length: %d\r\n\r\n%s" "$len" "$body" >&"${LSP[1]}"
}

recv_msg() {
    LAST_MSG=""
    local line content_length=0
    while IFS= read -r line <&"${LSP[0]}"; do
        line="${line%$'\r'}"
        [[ -z "$line" ]] && break
        if [[ "$line" == Content-Length:* ]]; then
            content_length="${line#Content-Length: }"
            content_length="${content_length//[[:space:]]/}"
        fi
    done
    [[ "$content_length" -le 0 ]] && return 1
    local buf=""
    while [[ ${#buf} -lt "$content_length" ]]; do
        local chunk
        IFS= read -r -n "$content_length" chunk <&"${LSP[0]}" || break
        buf="${buf}${chunk}"
    done
    LAST_MSG="$buf"
}

assert_contains() {
    local label="$1" substr="$2"
    if [[ "$LAST_MSG" == *"$substr"* ]]; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        echo "  Expected: $substr" >&2
        echo "  Got:      $LAST_MSG" >&2
        kill "$LSP_PID" 2>/dev/null || true
        exit 1
    fi
}

coproc LSP { exec "$HCC" --lsp 2>/dev/null; }
# named coproc use करने पर bash automatically LSP_PID set करता है
declare LSP_PID="${LSP_PID:-0}"

cleanup() { kill "$LSP_PID" 2>/dev/null || true; wait "$LSP_PID" 2>/dev/null || true; }
trap cleanup EXIT

# server initialize करो
send_msg '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":0,"capabilities":{},"rootUri":null}}'
recv_msg
assert_contains "initialize advertises semanticTokensProvider" '"semanticTokensProvider"'
assert_contains "legend has tokenTypes"   '"tokenTypes"'
assert_contains "legend includes keyword" '"keyword"'
assert_contains "legend includes type"    '"type"'

send_msg '{"jsonrpc":"2.0","method":"initialized","params":{}}'

# document open करो
DOC_URI="file:///tmp/sem_tok_test.HC"
SOURCE='I64 x = 42; U0 Main() { if (x > 0) { "%d\n", x; } }'
SOURCE_JSON="${SOURCE//\"/\\\"}"
send_msg "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\",\"languageId\":\"holyc\",\"version\":1,\"text\":\"${SOURCE_JSON}\"}}}"
recv_msg  # publishDiagnostics receive करो

# semantic tokens request करो
send_msg "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/semanticTokens/full\",\"params\":{\"textDocument\":{\"uri\":\"${DOC_URI}\"}}}"
recv_msg
assert_contains "semanticTokens/full response id" '"id":2'
assert_contains "semanticTokens/full has data field" '"data"'

# server shutdown करो
send_msg '{"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}}'
recv_msg
assert_contains "shutdown ok" '"id":3'
send_msg '{"jsonrpc":"2.0","method":"exit","params":{}}'
wait "$LSP_PID" 2>/dev/null || true

echo ""
echo "All LSP semantic token tests passed."
