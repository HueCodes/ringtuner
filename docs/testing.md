# Local Testing

Primary checks:

```sh
make test
make compare
make pareto
make trace
build/tune --scenario all --traffic scenario --train-seeds 1 --eval-seeds 1 --csv results/ci-tuning.csv
```

`make test` runs the full deterministic unit suite, including scenario tuning checks. The comparison, Pareto, trace, and small tuning commands mirror the dependency-free CI workflow.

Optional sanitizer check:

```sh
make ubsan
```

The sanitizer target builds the simulator with UndefinedBehaviorSanitizer, then runs a smoke subset with `--smoke`. `make asan` is a compatibility alias for this local check. AddressSanitizer is not part of the default sanitizer target because runtime behavior can vary in sandboxed local environments. The full deterministic suite remains in `make test`; under sanitizers at `-O0`, the broader suite can be much slower because many tests repeatedly compute metrics over large histograms.
