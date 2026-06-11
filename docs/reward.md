# Reward

Initial reward:

```text
reward = delivered_score - latency_penalty - tail_latency_penalty - drop_penalty - interrupt_cost_penalty - setting_change_penalty
```

The implementation uses normalized per-episode terms:

- delivered score rewards delivered packets relative to offered work.
- latency penalty uses average latency.
- tail latency penalty uses p99 latency.
- drop penalty is heavy.
- unresolved queue penalty discourages hiding packets in the ring at episode end.
- interrupt cost penalty discourages interrupt-per-packet behavior.
- setting change penalty discourages oscillation in the RL interface.

The episode metrics report cumulative reward for a completed run. The RL-facing `irq_sim_episode_step` returns delta reward for the most recent action interval to avoid repeatedly crediting prior history.

## Reward Hacking Risks

- Interrupt every packet to minimize latency.
- Coalesce forever to minimize interrupts.
- Drop packets to avoid latency accounting.
- Leave packets queued at episode end to avoid latency accounting.
- Overfit one traffic profile.
- Oscillate settings to chase transient reward.

## Mitigations

- Count dropped packets heavily.
- Include interrupt cost.
- Include tail latency.
- Penalize unresolved queue depth.
- Randomize traffic seeds.
- Evaluate mixed workloads.
- Penalize setting changes.
- Compare against baselines on unseen seeds.
