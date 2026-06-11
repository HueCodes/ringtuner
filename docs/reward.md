# Reward

Episode reward:

```text
reward = delivered_score - latency_penalty - tail_latency_penalty - drop_penalty - interrupt_cost_penalty - setting_change_penalty
         - unresolved_queue_penalty
```

The implementation computes these fields in `irq_reward_components_t`:

| component | formula | purpose |
| --- | --- | --- |
| `delivered_score` | `delivered / max(offered, 1)` | Reward completed work. |
| `latency_penalty` | `avg_latency / 200` | Penalize average latency. |
| `tail_latency_penalty` | `p99_latency / 400` | Penalize tail latency. |
| `drop_penalty` | `5 * dropped / max(offered, 1)` | Make drops dominate normal tuning noise. |
| `interrupt_cost_penalty` | `interrupt_cost / (max(offered, 1) * 50)` | Penalize interrupt-heavy behavior. |
| `setting_change_penalty` | `setting_changes * 0.001` | Discourage oscillation. |
| `unresolved_queue_penalty` | `3 * final_queue_depth / max(offered, 1)` | Penalize hiding packets in the ring at episode end. |

The episode metrics report cumulative reward for a completed run. The RL-facing `irq_sim_episode_step` returns delta reward for the most recent action interval to avoid repeatedly crediting prior history.

Initial policy setup before tick 0 does not count as a setting change. Runtime threshold changes do count.

Examples:

- On sparse traffic, `packet=1,timer=0` usually wins because latency is low and interrupt cost is still modest relative to offered work.
- On overload, all realistic CPU-limited policies are negative because drops dominate reward.
- `no_coalescing_oracle` is intentionally separated from realistic baselines because it can drain more than `service_budget` packets in one tick.
- `no_coalescing_cpu_limited` pays interrupt cost and can drop packets when offered load exceeds service capacity.

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
