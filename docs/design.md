# RingTuner Simulator Design

## Scope

Version 1 models one NIC RX queue with discrete ticks. Packet arrivals enter a fixed RX ring. Interrupt coalescing delays CPU service until either a packet threshold or timer threshold fires. The CPU drains a bounded batch per interrupt. Delivered packets accrue latency. Packets that arrive when the ring is full are dropped.

## System Model

- One RX queue and one CPU service path.
- One tick is the simulator time unit. It is intentionally abstract, but examples treat it like a small microsecond-scale slot.
- The RX ring has configurable capacity from 1 to `IRQ_SIM_MAX_RING`.
- Packet arrivals are generated before interrupt checks in each tick.
- CPU service occurs only on interrupt.
- One coalesced interrupt can fire per tick. The `no_coalescing` baseline is a special comparison mode that emits one interrupt per delivered packet.
- CPU service drains up to `service_budget` packets from the ring.

## Traffic

Traffic generation is deterministic from the seed and tick count. Profiles:

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

- delivered packets
- dropped packets
- interrupts
- interrupt cost
- p50, p95, and p99 latency
- average latency
- average queue depth
- reward score
- final queue depth

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
- fixed low latency
- fixed balanced
- fixed throughput
- simple adaptive

The adaptive baseline updates every 32 ticks. RL comparisons should use a stated control interval, since per-tick actions have more control authority.

## Limitations

- One queue only.
- No packet sizes, DMA descriptors, cache effects, NAPI polling, IRQ affinity, or PCIe modeling.
- CPU service time is compressed into one tick.
- Packet classes affect arrival shape only.
- Coalescing state is simpler than real NIC hardware.
- The no-coalescing baseline is an oracle-style comparison mode, not a hardware timing claim.
- Metrics are simulator metrics, not hardware claims.
