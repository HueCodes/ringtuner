# Sample Results

Command:

```sh
make run
```

Settings:

- ticks: 5000
- seed count: 1
- ring capacity: 256
- service budget: 32
- interrupt cost: 1.0

Selected rows from the local baseline run:

| traffic | policy | offered | delivered | drops | final queue | interrupts | irq per delivered | p99 latency | reward |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| zero_idle | fixed_balanced | 0 | 0 | 0 | 0 | 0 | 0.000 | 0.0 | 0.000 |
| steady_low | no_coalescing_oracle | 826 | 826 | 0 | 0 | 826 | 1.000 | 1.0 | 0.973 |
| steady_low | fixed_balanced | 826 | 826 | 0 | 0 | 231 | 0.280 | 17.0 | 0.898 |
| steady_low | adaptive_bandit | 826 | 826 | 0 | 0 | 497 | 0.602 | 63.0 | 0.731 |
| steady_high | no_coalescing_cpu_limited | 5499 | 5499 | 0 | 0 | 4402 | 0.801 | 1.0 | 0.975 |
| steady_high | fixed_throughput | 5499 | 5472 | 0 | 27 | 171 | 0.031 | 32.0 | 0.824 |
| overload_spike | no_coalescing_oracle | 42548 | 42548 | 0 | 0 | 42548 | 1.000 | 1.0 | 0.973 |
| overload_spike | no_coalescing_cpu_limited | 42548 | 32707 | 9841 | 0 | 1489 | 0.046 | 8.0 | -0.447 |
| overload_spike | fixed_balanced | 42548 | 32704 | 9844 | 0 | 1172 | 0.036 | 8.0 | -0.448 |
| overload_spike | adaptive_bandit | 42548 | 32706 | 9842 | 0 | 1388 | 0.042 | 8.0 | -0.481 |

Interpretation:

- The oracle baseline is a latency reference, not a feasible overload policy.
- The CPU-limited no-coalescing baseline shows realistic drops when offered load exceeds service capacity.
- Fixed throughput reduces interrupts but can leave queued packets at episode end or increase tail latency.
- The adaptive bandit is deterministic and dependency-free, but it is not state-conditioned RL and does not beat tuned thresholds here.
- Under overload, drop penalties dominate reward as intended.

Limitations:

- This is a deterministic synthetic simulator result, not a hardware benchmark.
- The sample uses one seed. Tuning results in `docs/tuning-results.md` use train and held-out seed sets.
