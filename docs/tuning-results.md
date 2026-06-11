# Offline Tuning Results

Command:

```sh
make tune
```

This runs a deterministic grid search over packet thresholds and timer thresholds. It is not RL training. It is a baseline tuning run that gives a useful comparison target for a later learner.

Settings:

- ticks: 5000
- seeds: 16
- packet grid: `1, 2, 4, 8, 12, 16, 32, 64`
- timer grid: `0, 1, 2, 4, 8, 16, 32, 64, 128`
- output: `results/tuning-grid.csv`

Best settings from this local run:

| traffic | packet threshold | timer threshold | average reward | average p99 | average interrupts | average drops |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| steady_low | 1 | 0 | 0.972500 | 1.000 | 780.562 | 0.000 |
| steady_high | 2 | 1 | 0.978227 | 2.000 | 2625.250 | 0.000 |
| bursty | 1 | 0 | 0.986829 | 1.000 | 801.250 | 0.000 |
| elephant_mouse | 1 | 0 | 0.984731 | 1.000 | 2209.000 | 0.000 |
| overload_spike | 1 | 0 | -0.451831 | 8.000 | 1481.500 | 9879.438 |

Interpretation:

- The current reward strongly favors low latency for non-overload profiles.
- Overload remains negative because drops dominate reward once offered load exceeds service capacity.
- These tuned thresholds are useful baselines, but they are not a learned adaptive policy.
- A later RL run should compare against these grid-search results on unseen seeds.

