#!/bin/sh
set -eu

BUILD=${BUILD:-build}

require_grep() {
    pattern=$1
    file=$2
    if ! grep -q "$pattern" "$file"; then
        printf 'FAIL: expected pattern "%s" in %s\n' "$pattern" "$file" >&2
        exit 1
    fi
}

require_lines_at_least() {
    file=$1
    min_lines=$2
    lines=$(wc -l < "$file")
    if [ "$lines" -lt "$min_lines" ]; then
        printf 'FAIL: expected at least %s lines in %s, got %s\n' "$min_lines" "$file" "$lines" >&2
        exit 1
    fi
}

mkdir -p results

"$BUILD/ringtuner" --list-profiles > results/test-cli-profiles.txt
require_grep '^3 bursty$' results/test-cli-profiles.txt

"$BUILD/ringtuner" --trace traces/microburst.csv --policy fixed_balanced --csv results/test-cli-trace.csv > results/test-cli-trace.out
require_grep '^scenario,traffic,policy,offered,delivered,drops,' results/test-cli-trace.csv
require_grep '^default,' results/test-cli-trace.csv

if "$BUILD/ringtuner" --csv ../bad.csv > results/test-cli-bad-path.out 2>&1; then
    printf 'FAIL: unsafe csv path accepted\n' >&2
    exit 1
fi
require_grep 'csv path must be under results/' results/test-cli-bad-path.out

"$BUILD/tune" \
    --scenario small_rx_ring_stress \
    --traffic scenario \
    --train-seeds 1 \
    --eval-seeds 1 \
    --packet-grid 1,2 \
    --timer-grid 0,1 \
    --csv results/test-cli-tune.csv > results/test-cli-tune.out
require_grep '^small_rx_ring_stress,candidate,bursty,' results/test-cli-tune.csv
require_grep '^small_rx_ring_stress,best_global_worst,scenario,' results/test-cli-tune.csv

"$BUILD/compare" \
    --scenario small_rx_ring_stress \
    --train-seeds 1 \
    --eval-seeds 1 \
    --packet-grid 1,2 \
    --timer-grid 0,1 \
    --csv results/test-cli-compare.csv > results/test-cli-compare.out
require_grep '^small_rx_ring_stress,fixed_balanced,' results/test-cli-compare.csv
require_grep '^small_rx_ring_stress,tuned_direct,' results/test-cli-compare.csv

"$BUILD/pareto" \
    --scenario small_rx_ring_stress \
    --traffic scenario \
    --seeds 1 \
    --packet-grid 1,2 \
    --timer-grid 0,1 \
    --csv results/test-cli-pareto.csv > results/test-cli-pareto.out
require_grep '^small_rx_ring_stress,scenario,bursty,' results/test-cli-pareto.csv

"$BUILD/report" \
    --comparison results/test-cli-compare.csv \
    --pareto results/test-cli-pareto.csv \
    --trace results/test-cli-trace.csv \
    --out results/test-cli-report.md
require_grep '^## Scenario Comparison$' results/test-cli-report.md
require_lines_at_least results/test-cli-report.md 8
