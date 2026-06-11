# Offline Tuning Results

Command:

```sh
make tune
```

This runs a deterministic grid search over packet thresholds and timer thresholds. It is not RL training. It is a baseline tuning run that gives a useful comparison target for a later learner.

Settings:

- ticks: 5000
- train seeds: 16
- eval seeds: 16
- packet grid: `1, 2, 4, 8, 12, 16, 32, 64`
- timer grid: `0, 1, 2, 4, 8, 16, 32, 64, 128`
- output: `results/tuning-grid.csv`
- held-out eval seeds start at seed 10001

Best settings from this local run:

| selection | packet threshold | timer threshold | train reward | eval reward | eval p99 | eval interrupts | eval drop ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero_idle | 1 | 0 | 0.000000 | 0.000000 | 0.000 | 0.000 | 0.000000 |
| steady_low | 1 | 0 | 0.972500 | 0.972500 | 1.000 | 787.062 | 0.000000 |
| steady_high | 2 | 1 | 0.978227 | 0.978402 | 2.000 | 2634.125 | 0.000000 |
| bursty | 1 | 0 | 0.986829 | 0.986784 | 1.000 | 808.438 | 0.000000 |
| elephant_mouse | 1 | 0 | 0.984731 | 0.984728 | 1.000 | 2206.250 | 0.000000 |
| overload_spike | 1 | 0 | -0.451831 | -0.460301 | 8.000 | 1483.750 | 0.233434 |
| global_mean | 1 | 0 | 0.578097 | 0.576689 | n/a | n/a | n/a |
| global_worst | 1 | 0 | -0.451831 | -0.460301 | n/a | n/a | n/a |

Interpretation:

- The current reward strongly favors low latency for non-overload profiles.
- `zero_idle` is neutral and exists to test accounting and no-arrival reward behavior.
- Overload remains negative because drops dominate reward once offered load exceeds service capacity.
- Mean and worst-case global tuning currently choose the same thresholds because the overload reward is still best at immediate service.
- These tuned thresholds are useful baselines, but they are not a learned adaptive policy.
- A later RL run should compare against these grid-search results on unseen seeds and scenarios.
