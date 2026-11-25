#!/usr/bin/env bash
# mayhem/test.sh — run upstream's own rclcpp gtest files (built by mayhem/build.sh against the
# normal-flag rclcpp) and report CTRF counts parsed from gtest's own summary lines.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

B="$SRC/mayhem/build/tests"
SUITES="test_parameter test_parameter_map test_parameter_events_filter"
passed=0 failed=0 skipped=0
export ROS_LOG_DIR="${TMPDIR:-/tmp}/rclcpp-test-logs"
for s in $SUITES; do
  if [ ! -x "$B/$s" ]; then echo "missing test binary $B/$s (build.sh bug)" >&2; failed=$((failed+1)); continue; fi
  out="$("$B/$s" 2>&1)"; rc=$?
  echo "$out" | tail -n 3
  p=$(echo "$out" | sed -n 's/^\[  PASSED  \] \([0-9]*\) test.*/\1/p' | tail -1)
  f=$(echo "$out" | sed -n 's/^\[  FAILED  \] \([0-9]*\) test.*/\1/p' | tail -1)
  k=$(echo "$out" | sed -n 's/^\[  SKIPPED \] \([0-9]*\) test.*/\1/p' | tail -1)
  p=${p:-0}; f=${f:-0}; k=${k:-0}
  # No gtest summary or a non-zero exit without counted failures => the suite itself failed.
  if [ "$p" -eq 0 ] && [ "$f" -eq 0 ]; then f=1; fi
  if [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then f=1; fi
  passed=$((passed+p)); failed=$((failed+f)); skipped=$((skipped+k))
done
emit_ctrf "gtest" "$passed" "$failed" "$skipped"
