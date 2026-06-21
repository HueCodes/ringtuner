# RingTuner Simulator Design

## Scope

Version 1 models one NIC RX queue with discrete ticks. Packet arrivals enter a fixed RX ring. Interrupt coalescing delays CPU service until either a packet threshold or timer threshold fires. The CPU drains a bounded batch per interrupt. Delivered packets accrue latency. Packets that arrive when the ring is full are dropped.

## System Model

- One RX queue and one CPU service path.
- One tick is the simulator time unit. It is intentionally abstract, but examples treat it like a small microsecond-scale slot.
- The RX ring has configurable capacity from 1 to `IRQ_SIM_MAX_RING`.
- Packet arrivals are generated before interrupt checks in each tick.
- CPU service occurs only on interrupt.
- One coalesced interrupt can fire per tick.
- CPU service drains up to `service_budget` packets from the ring.
- `no_coalescing_oracle` is an idealized comparison mode that can emit one interrupt per queued packet in one tick.
- `no_coalescing_cpu_limited` interrupts as soon as packets are queued, but still drains at most `service_budget` packets in a tick.

## Traffic

Traffic generation is deterministic from the seed and tick count. Profiles:

- `zero_idle`: no arrivals, used for edge-case accounting.
- `steady_low`: sparse Bernoulli arrivals.
- `steady_high`: frequent arrivals with occasional two-packet ticks.
- `bursty`: low background plus periodic bursts.
- `elephant_mouse`: frequent mouse packets with longer elephant bursts.
- `overload_spike`: light traffic, then a spike above the default service budget.

Packet classes are modeled only through traffic shape. Packet sizes are not modeled in version 1.

## Coalescing

The simulator tracks when the queue first becomes nonempty. An interrupt fires when either condition is true:

- `queue_depth >= packet_threshold`
- `ticks_since_first_queued_packet >= timer_threshold`

`packet_threshold` and `timer_threshold` are clamped to valid ranges. A threshold of 1 approximates no packet coalescing. A timer threshold of 0 means interrupt as soon as packets are queued.

## Drops

Arrivals are dropped when the RX ring is full. Drops are counted immediately. Dropped packets do not contribute latency samples, which is why reward functions must penalize drops directly.

## Metrics

The simulator records:

- offered packets
- delivered packets
- dropped packets
- delivered ratio
- drop ratio
- interrupts
- interrupts per delivered packet
- average batch size
- interrupt cost
- p50, p95, and p99 latency
- latency sample count
- average latency
- average queue depth
- p50, p95, and p99 queue depth
- max queue depth
- reward score
- final queue depth
- reward components

Latency is measured as `delivery_tick - arrival_tick + 1`, so same-tick delivery has latency 1.

## Episode

An episode runs for `episode_ticks` ticks. Termination occurs when the configured tick count is reached. Reset clears all state and initializes deterministic RNG from the seed.

## Determinism

The simulator uses a local xorshift64 generator. No external entropy is used after reset. Repeating the same seed, config, traffic profile, and action sequence produces identical metrics.

## Observations

Initial RL-facing observation vector:

- queue depth normalized
- recent arrival rate normalized
- recent delivered rate normalized
- recent drop rate normalized
- recent average latency normalized
- recent max latency normalized
- current packet threshold normalized
- current timer threshold normalized
- recent interrupt rate normalized

All observation values are finite and clamped to `[0, 1]`. The RL-facing `irq_sim_episode_step` clears the recent window before each action, advances one tick, and returns observations for that action interval.

## Actions

Initial discrete actions:

- low latency mode
- balanced low
- balanced high
- throughput mode
- bulk mode

Actions map to stable packet and timer thresholds. Invalid actions are rejected.

## Baselines

Baseline policies:

- no coalescing oracle
- no coalescing CPU-limited
- fixed low latency
- fixed balanced
- fixed throughput
- simple adaptive
- adaptive bandit
- NAPI-style polling

The simple adaptive baseline updates every 32 ticks. The adaptive bandit baseline uses deterministic epsilon-greedy selection over the discrete action set with a 64-tick control window. It is an offline adaptive controller baseline, not PPO or deep RL. RL comparisons should use a stated control interval, since per-tick actions have more control authority.

The NAPI-style polling baseline enters poll mode after an interrupt and drains up to `service_budget` packets on later ticks without counting those polls as hardware interrupts. Poll mode exits after two idle polls. This is a scheduler-level approximation, not a Linux driver model.

## Control Interval

`irq_sim_run_control_loop` runs any action selector callback at a fixed control interval. It applies one action, advances up to `control_interval` ticks, then exposes an observation for the next decision. This creates a fair comparison point for fixed actions, heuristic controllers, the adaptive bandit, and future RL policies.

## Limitations

- One queue only.
- No packet sizes, DMA descriptors, cache effects, IRQ affinity, or PCIe modeling.
- CPU service time is compressed into one tick.
- Packet classes affect arrival shape only.
- Coalescing state is simpler than real NIC hardware.
- NAPI-style polling is simplified to poll-mode scheduling and batch draining.
- The no-coalescing oracle baseline is not a hardware timing claim.
- Metrics are simulator metrics, not hardware claims.

## Scenarios

The C core exposes a small scenario suite:

- `latency_sensitive_low_load`
- `high_throughput_steady_load`
- `microburst_workload`
- `elephant_flow_burst`
- `overload_with_recovery`
- `small_rx_ring_stress`
- `low_cpu_budget_stress`

Scenarios vary ring capacity, service budget, traffic profile, episode length, interrupt cost, and tuning threshold ranges. The benchmark CLI can run all scenarios with `build/ringtuner --scenario all`. The tuning CLI can tune one scenario with `build/tune --scenario NAME` or every scenario with `build/tune --scenario all`. Tuning can optimize all traffic profiles, one named traffic profile, or each scenario's declared traffic profile with `--traffic scenario`.
