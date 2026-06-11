# RingTuner

`RingTuner: RL-Tuned NIC Interrupt Coalescing Simulator` is a standalone C simulator for NIC RX interrupt coalescing. It is built as a small systems/RL project that can later become a PufferLib Ocean environment.

## Why Interrupt Coalescing Matters

NICs can interrupt the CPU for every received packet, which minimizes latency but burns CPU. They can also wait for more packets or a timer, which reduces interrupt overhead but increases latency and tail latency. Under overload, the wrong settings can fill the RX ring and drop packets.

RingTuner models that tradeoff with one RX queue, deterministic traffic, a fixed-size ring, packet and timer interrupt thresholds, bounded CPU service, latency metrics, drops, and baseline policies.

The `no_coalescing` baseline is an oracle-style comparison mode. It is useful as a lower-latency reference, but it is not a claim about real CPU behavior under overload.

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

Run direct fixed thresholds:

```sh
build/ringtuner --direct --profile steady_high --packet-threshold 4 --timer-threshold 8
```

Run the offline threshold tuning sweep:

```sh
make tune
```

Example output:

```text
traffic          policy              delivered    drops interrupts   irq_cost      p50      p95      p99    reward       ms
steady_low       no_coalescing             826        0        826      826.0      1.0      1.0      1.0     0.972     0.06
steady_low       fixed_balanced            826        0        231      231.0     12.0     17.0     17.0     0.898     0.02
overload_spike   simple_adaptive         32704     9844       1260     1260.0      8.0      8.0      8.0    -0.451     0.19
```

## Current Model

- One RX queue.
- Discrete ticks.
- Deterministic seeded traffic profiles.
- Fixed RX ring capacity.
- Packet threshold and timer threshold coalescing.
- CPU drains up to `service_budget` packets per interrupt.
- Metrics include delivered packets, drops, interrupts, interrupt cost, p50/p95/p99 latency, average queue depth, and reward.

## RL-Ready API

The C core exposes reset, step, action application, normalized observation export, reward calculation, and baseline policy helpers in `src/irq_sim.h`.

Initial discrete actions:

- low latency
- balanced low
- balanced high
- throughput
- bulk

## Limitations

This is not a hardware-accurate NIC model. It omits packet sizes, DMA descriptors, cache effects, NAPI polling, IRQ affinity, PCIe behavior, multiple queues, and explicit CPU service time. Results are useful for simulator and RL design work, not hardware claims.

## Next Step

Compare a real RL learner against the grid-search baselines in `docs/tuning-results.md`, then wrap the standalone C core as a PufferLib Ocean environment.
