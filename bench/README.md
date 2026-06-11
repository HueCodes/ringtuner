# Benchmarks

Local benchmark commands:

```sh
make run
make tune
```

Run one profile and one policy:

```sh
build/ringtuner --profile steady_high --policy fixed_balanced
```

Run direct fixed coalescing settings:

```sh
build/ringtuner --direct --profile steady_high --packet-threshold 4 --timer-threshold 8
```

Write benchmark CSV:

```sh
build/ringtuner --csv results/baselines.csv
```

Write tuning CSV:

```sh
build/tune --csv results/tuning-grid.csv
```

## Baseline CSV Schema

- `traffic`
- `policy`
- `offered`
- `delivered`
- `drops`
- `interrupts`
- `final_queue_depth`
- `avg_queue_depth`
- `interrupt_cost`
- `p50`
- `p95`
- `p99`
- `reward`
- `runtime_ms`

## Tuning CSV Schema

- `traffic`
- `packet_threshold`
- `timer_threshold`
- `avg_reward`
- `avg_p99`
- `avg_interrupts`
- `avg_drops`
- `delivered`
- `dropped`
- `interrupts`

