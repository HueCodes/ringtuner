# RingTuner

RingTuner is a standalone C simulator for NIC RX interrupt coalescing. It models a small systems control problem with deterministic traffic, fixed baselines, a simple adaptive bandit baseline, offline threshold tuning, and an RL-ready step API that can later become a PufferLib Ocean environment.

## Why Interrupt Coalescing Matters

NICs can interrupt the CPU for every received packet, which minimizes latency but burns CPU. They can also wait for more packets or a timer, which reduces interrupt overhead but increases latency and tail latency. Under overload, the wrong settings can fill the RX ring and drop packets.

RingTuner models that tradeoff with one RX queue, deterministic traffic, a fixed-size ring, packet and timer interrupt thresholds, bounded CPU service, latency metrics, drops, and baseline policies.

## Why This Is Interesting

- Interrupt moderation is a real systems tradeoff.
- Latency, throughput, drops, and CPU overhead conflict.
- RL control is plausible, but only after strong fixed and tuned baselines exist.
- The simulator is small enough to audit and fast enough to sweep.

The `no_coalescing_oracle` baseline is an idealized comparison mode. It drains queued packets with one interrupt per packet in the same tick and is useful as a latency reference. The `no_coalescing_cpu_limited` baseline interrupts as soon as packets arrive but still drains at most `service_budget` packets per tick.

## Build

```sh
make
```

## Test

```sh
make test
```

Optional sanitizer target:

```sh
make asan
```

## Run Baselines

```sh
make run
```

Write CSV results:

```sh
make
build/ringtuner --csv results/baselines.csv
```

Run one profile and policy:

```sh
build/ringtuner --profile steady_high --policy fixed_balanced
```

Run one fixed action through the control-loop wrapper:

```sh
build/ringtuner --profile bursty --action balanced_low --control-interval 32
```

Run direct fixed thresholds:

```sh
build/ringtuner --direct --profile steady_high --packet-threshold 4 --timer-threshold 8
```

Run the offline threshold tuning sweep:

```sh
make tune
```

Tune within one scenario's declared threshold range:

```sh
build/tune --scenario small_rx_ring_stress --csv results/tuning-small-ring.csv
```

Tune every built-in scenario as a matrix:

```sh
make tune-scenarios
```

Tune each scenario only against its declared traffic profile:

```sh
make tune-scenario-traffic
```

Compare tuned thresholds against realistic baselines:

```sh
make compare
```

Run tests, baselines, scenarios, tuning, and a generated local summary:

```sh
make report
```

Example output:

```text
scenario                  traffic          policy                       offered delivered   drops  finalq interrupts  irq/del      p50      p95      p99    reward       ms
default                   steady_low       no_coalescing_oracle             826       826       0       0     826    1.000      1.0      1.0      1.0     0.973     0.06
default                   steady_low       fixed_balanced                   826       826       0       0     231    0.280     12.0     17.0     17.0     0.898     0.02
default                   overload_spike   no_coalescing_cpu_limited      42548     32707    9841       0    1489    0.046      8.0      8.0      8.0    -0.447     0.20
```

## Current Model

- One RX queue.
- Discrete ticks.
- Deterministic seeded traffic profiles, including a zero-arrival edge profile.
- Fixed RX ring capacity.
- Packet threshold and timer threshold coalescing.
- CPU drains up to `service_budget` packets per interrupt.
- Metrics include offered packets, delivered and drop ratios, interrupts per delivered packet, average batch size, final and max queue depth, queue occupancy percentiles, latency percentiles, unresolved queue depth, reward components, and total reward.
- Scenario definitions include threshold ranges for scenario-scoped and all-scenario tuning. The tuner can optimize all traffic profiles, a named traffic profile, or each scenario's declared traffic profile.

## RL-Ready API

The C core exposes reset, step, action application, fixed-interval control-loop execution, normalized observation export, reward calculation, and baseline policy helpers in `src/irq_sim.h`.

Initial discrete actions:

- low latency
- balanced low
- balanced high
- throughput
- bulk

## Limitations

This is not a hardware-accurate NIC model. It omits packet sizes, DMA descriptors, cache effects, NAPI polling, IRQ affinity, PCIe behavior, multiple queues, and explicit CPU service time. Results are useful for simulator and RL design work, not hardware claims.

## Next Step

Compare a real RL learner against the grid-search and adaptive bandit baselines, then wrap the standalone C core as a PufferLib Ocean environment.
