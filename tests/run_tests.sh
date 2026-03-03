#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# HolyC test runner — सभी tests चलाओ
# Usage: run_tests.sh [-v]  (-v verbose mode के लिए)
#
# TESTS_DIR के नीचे हर .HC file जिसके साथ .expected file है उसे
# तीन modes में चलाओ: --interpret, --jit, और AOT (compile + execute)।
# Output को temp file में capture करो (command substitution नहीं) ताकि
# trailing newlines shell में silently drop न हों।
# ---------------------------------------------------------------------------

HCC=/home/nk0/HolyC/build/hcc
TESTS_DIR=/home/nk0/HolyC/tests
INCLUDE_DIR=/home/nk0/HolyC
AOT_BIN=/tmp/holyc_test_bin
export ASAN_OPTIONS=detect_leaks=0

VERBOSE=0
if [[ "${1:-}" == "-v" ]]; then
  VERBOSE=1
fi

# Colors (stdout tty नहीं है तो disable करो)
if [[ -t 1 ]]; then
  GREEN='\033[0;32m'
  RED='\033[0;31m'
  RESET='\033[0m'
else
  GREEN='' RED='' RESET=''
fi

PASS_COUNT=0
FAIL_COUNT=0

# output capture के लिए temp file; exit पर cleanup करो
TMPOUT=$(mktemp /tmp/holyc_test_out.XXXXXX)
TMPERR=$(mktemp /tmp/holyc_test_err.XXXXXX)
trap 'rm -f "$TMPOUT" "$TMPERR" "$AOT_BIN"' EXIT

# हर .HC file collect करो जिसके साथ matching .expected file हो
mapfile -t HC_FILES < <(
  find "$TESTS_DIR" -name "*.HC" | sort | while read -r hc; do
    [[ -f "${hc%.HC}.expected" ]] && printf '%s\n' "$hc"
  done
)

if [[ ${#HC_FILES[@]} -eq 0 ]]; then
  echo "No test files found under $TESTS_DIR"
  exit 1
fi

# ---------------------------------------------------------------------------
# report_result <label> <expected_file> <exit_code> — result check करने का function
#   $TMPOUT को expected_file से diff करो और PASS/FAIL print करो।
#   PASS पर 0 return करो, FAIL पर 1।
# ---------------------------------------------------------------------------
report_result() {
  local label="$1"
  local expected="$2"
  local exit_code="$3"

  if diff -q "$TMPOUT" "$expected" > /dev/null 2>&1; then
    printf "    [${GREEN}PASS${RESET}] %s\n" "$label"
    return 0
  else
    printf "    [${RED}FAIL${RESET}] %s\n" "$label"
    if [[ $VERBOSE -eq 1 ]]; then
      echo "      --- expected ---"
      sed 's/^/      /' "$expected"
      echo "      --- got (exit=$exit_code) ---"
      sed 's/^/      /' "$TMPOUT"
      echo "      --- diff (got vs expected) ---"
      diff "$TMPOUT" "$expected" | sed 's/^/      /' || true
    fi
    return 1
  fi
}

# ---------------------------------------------------------------------------
# run_mode <label> <expected_file> <cmd> [args...] — एक mode में test चलाओ
#   <cmd> [args...] चलाओ, stdout को $TMPOUT में capture करो (stderr discard),
#   फिर report_result call करो। PASS पर 0, FAIL पर 1 return करो।
# ---------------------------------------------------------------------------
run_mode() {
  local label="$1"
  local expected="$2"
  shift 2

  local exit_code=0
  "$@" > "$TMPOUT" 2>/dev/null || exit_code=$?

  report_result "$label" "$expected" "$exit_code"
}

# ---------------------------------------------------------------------------
# run_aot <label> <hc_file> <expected_file> — AOT compile करके test चलाओ
#   native binary में compile करो, फिर execute करो।
#   PASS पर 0, FAIL पर 1 return करो।
# ---------------------------------------------------------------------------
run_aot() {
  local label="$1"
  local hc_file="$2"
  local expected="$3"

  # Compile करो
  local compile_exit=0
  "$HCC" "$hc_file" -I "$INCLUDE_DIR" -o "$AOT_BIN" > "$TMPERR" 2>&1 \
    || compile_exit=$?

  if [[ $compile_exit -ne 0 ]]; then
    printf "    [${RED}FAIL${RESET}] %s  (compile error, exit=%d)\n" \
      "$label" "$compile_exit"
    if [[ $VERBOSE -eq 1 ]]; then
      sed 's/^/      /' "$TMPERR"
    fi
    return 1
  fi

  # Execute करो
  run_mode "$label" "$expected" "$AOT_BIN"
}

# ---------------------------------------------------------------------------
# Main loop — सभी test files पर iterate करो
# ---------------------------------------------------------------------------
echo "Running ${#HC_FILES[@]} test(s) under $TESTS_DIR"
echo "HCC: $HCC"
echo "========================================"

for hc_file in "${HC_FILES[@]}"; do
  expected="${hc_file%.HC}.expected"
  rel="${hc_file#"$TESTS_DIR/"}"

  printf "\n%s\n" "$rel"

  # test file में skip markers check करो
  skip_jit=0; skip_aot=0; skip_interpret=0; skip_format=0
  grep -q 'skip-jit'       "$hc_file" 2>/dev/null && skip_jit=1
  grep -q 'skip-aot'       "$hc_file" 2>/dev/null && skip_aot=1
  grep -q 'skip-interpret' "$hc_file" 2>/dev/null && skip_interpret=1
  grep -q 'skip-format'    "$hc_file" 2>/dev/null && skip_format=1

  # तीनों execution modes skip हैं लेकिन format नहीं, तो --format चलाओ
  if [[ $skip_interpret -eq 1 && $skip_jit -eq 1 && $skip_aot -eq 1 \
        && $skip_format -eq 0 ]]; then
    if run_mode "format " "$expected" \
        "$HCC" "--format" "$hc_file"; then
      (( PASS_COUNT++ )) || true
    else
      (( FAIL_COUNT++ )) || true
    fi
    continue
  fi

  # interpret mode — interpreter से चलाओ
  if [[ $skip_interpret -eq 1 ]]; then
    printf "    [SKIP] interpret\n"
  elif run_mode "interpret" "$expected" \
      "$HCC" "--interpret" "$hc_file" "-I" "$INCLUDE_DIR"; then
    (( PASS_COUNT++ )) || true
  else
    (( FAIL_COUNT++ )) || true
  fi

  # jit mode — JIT से चलाओ
  if [[ $skip_jit -eq 1 ]]; then
    printf "    [SKIP] jit\n"
  elif run_mode "jit    " "$expected" \
      "$HCC" "--jit" "$hc_file" "-I" "$INCLUDE_DIR"; then
    (( PASS_COUNT++ )) || true
  else
    (( FAIL_COUNT++ )) || true
  fi

  # aot mode — AOT compile करके चलाओ
  if [[ $skip_aot -eq 1 ]]; then
    printf "    [SKIP] aot\n"
  elif run_aot "aot    " "$hc_file" "$expected"; then
    (( PASS_COUNT++ )) || true
  else
    (( FAIL_COUNT++ )) || true
  fi
done

# ---------------------------------------------------------------------------
# Summary — कितने pass, कितने fail
# ---------------------------------------------------------------------------
TOTAL=$(( PASS_COUNT + FAIL_COUNT ))
echo ""
echo "========================================"
printf "Results: ${GREEN}%d passed${RESET} / ${RED}%d failed${RESET} / %d total  (%d test file(s))\n" \
  "$PASS_COUNT" "$FAIL_COUNT" "$TOTAL" "${#HC_FILES[@]}"
echo "========================================"

if [[ $FAIL_COUNT -gt 0 ]]; then
  echo "Some tests FAILED.  Re-run with -v for details."
  exit 1
fi

echo "All tests passed."
exit 0
