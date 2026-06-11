# Trace Replay

RingTuner can replay deterministic arrival traces from `traces/*.csv`.

Command:

```sh
make trace
```

Direct CLI example:

```sh
build/ringtuner --trace traces/microburst.csv --policy fixed_balanced --csv results/trace-microburst.csv
```

Trace format:

```csv
tick,arrivals
0,0
1,1
2,0
3,4
```

Rules:

- trace paths must stay under `traces/`
- CSV output paths must stay under `results/`
- omitted ticks replay as zero arrivals
- the episode length becomes the last trace tick plus one
- `--trace` can run fixed baselines or `--direct` thresholds
- `--trace` is intentionally separate from synthetic `--scenario` runs

The fixture in `traces/microburst.csv` exists for reproducible smoke tests and report generation. It is not a hardware trace.
