#!/usr/bin/env bash
# Turn a failed build log into GitHub annotations (readable without the log).
#   tools/ci-report.sh build.log
log="${1:-build.log}"
[ -f "$log" ] || exit 0
enc() { sed 's/%/%25/g' | awk 'BEGIN{ORS="%0A"} {print}'; }
echo "::error title=failed targets::$(grep -E '\] Error [0-9]|FAILED:' "$log" | head -20 | enc)"
n=$(grep -n -m1 -E 'error:' "$log" | cut -d: -f1)
if [ -n "$n" ]; then
    s=$(( n > 30 ? n - 30 : 1 ))
    echo "::error title=first error context::$(sed -n "${s},$((n + 8))p" "$log" | enc)"
fi
echo "::error title=build.log tail::$(tail -60 "$log" | enc)"
