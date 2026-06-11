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
- CSV columns include the scenario name, so default and scenario-scoped sweeps can be compared safely.
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

## Scenario-Scoped Tuning

Command:

```sh
build/tune --scenario small_rx_ring_stress --csv results/tuning-small-ring.csv
```

When `--scenario` is set, the tuner uses the scenario's ring capacity, service budget, traffic profile, episode length, interrupt cost, and threshold bounds. The default packet and timer grids are filtered to the scenario's declared tuning range unless explicit `--packet-grid` or `--timer-grid` values are provided.

Traffic selection:

- `--traffic all` tunes across every traffic profile. This is the default.
- `--traffic NAME` tunes one traffic profile by name or index.
- `--traffic scenario` tunes only the scenario's declared traffic profile and requires `--scenario NAME` or `--scenario all`.

Run the full scenario suite:

```sh
make tune-scenarios
```

This calls `build/tune --scenario all` and writes `results/tuning-scenarios.csv`. Each scenario is tuned independently with its own config and bounds, which makes the CSV suitable for comparing how tuned thresholds move as ring size, service budget, traffic shape, episode length, and interrupt cost change.

Run the scenario suite against only each scenario's declared traffic:

```sh
make tune-scenario-traffic
```

This writes `results/tuning-scenario-traffic.csv`. It is the cleaner scenario-fit artifact, while `results/tuning-scenarios.csv` is the broader stress matrix that evaluates all traffic profiles under each scenario config.
