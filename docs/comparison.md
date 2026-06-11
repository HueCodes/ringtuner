# Tuned Baseline Comparison

`build/compare` compares scenario-declared-traffic tuned thresholds against realistic baselines.

Command:

```sh
make compare
```

The comparison flow:

- tunes packet and timer thresholds on train seeds for each scenario's declared traffic profile
- evaluates fixed low latency, fixed balanced, fixed throughput, simple adaptive, adaptive bandit, and tuned direct thresholds on held-out seeds
- reports reward delta versus `fixed_balanced`
- writes `results/comparison.csv`

The comparison intentionally excludes `no_coalescing_oracle` because it is an idealized reference, not a feasible production policy.
