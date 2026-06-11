# Fixed Control Loop

RingTuner exposes `irq_sim_run_control_loop` for fair action-cadence comparisons.

The wrapper:

- resets a simulator instance
- exports the current observation
- asks a callback for one `irq_action_t`
- applies that action
- advances exactly `control_interval` ticks, or stops at episode end
- clears recent counters when a new control interval starts
- returns final episode metrics

This makes it possible to compare fixed actions, heuristic controllers, bandit controllers, and future RL policies at the same control cadence.

CLI example:

```sh
build/ringtuner --profile bursty --action balanced_low --control-interval 32
```

This is different from calling `irq_sim_episode_step` every tick. Per-tick control has more authority and should be reported separately.
