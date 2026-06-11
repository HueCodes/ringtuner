# Pareto Frontier

`build/pareto` finds non-dominated packet and timer thresholds over the same tuning grid used by `build/tune`.

Command:

```sh
make pareto
```

Direct CLI example:

```sh
build/pareto --scenario all --traffic scenario --csv results/pareto.csv
```

Dominance uses these objectives:

- higher reward
- higher delivered ratio
- lower p99 latency
- lower interrupt count
- lower drop ratio
- higher mean and worst reward across the selected traffic set

The output keeps only frontier rows. Use it to inspect viable tradeoffs that a single scalar reward can hide.
