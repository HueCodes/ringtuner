# PufferLib Integration Plan

## Likely Files

- `src/irq_sim.h` and `src/irq_sim.c` remain the portable simulator core.
- A future `pufferlib/` or `envs/` wrapper can own Ocean-specific glue.
- Build integration should compile the C core without changing simulator semantics.

## API Mapping

- Environment reset maps to `irq_sim_reset`.
- Environment step maps to `irq_sim_episode_step`, which applies an action, advances one tick, returns interval observations, and reports delta reward.
- Fixed-cadence evaluation maps to `irq_sim_run_control_loop`, which calls an action selector every configured control interval.
- Observation export maps to `irq_sim_observation`.
- Reward maps to `irq_sim_reward`.
- Episode done maps to the configured tick limit.
- Metrics export maps to `irq_sim_metrics`.
- Reward component export maps to `irq_sim_reward_components`.

## Observation, Action, Reward

The current observation vector has nine normalized `double` values:

1. queue depth
2. recent arrival rate
3. recent delivered rate
4. recent drop rate
5. recent average latency
6. recent max latency
7. current packet threshold
8. current timer threshold
9. recent interrupt rate

Actions map to stable enum values:

| action | enum |
| --- | --- |
| low latency | `IRQ_ACTION_LOW_LATENCY` |
| balanced low | `IRQ_ACTION_BALANCED_LOW` |
| balanced high | `IRQ_ACTION_BALANCED_HIGH` |
| throughput | `IRQ_ACTION_THROUGHPUT` |
| bulk | `IRQ_ACTION_BULK` |

Step reward is finite and includes delivery, latency, tail latency, unresolved queue depth, drops, interrupt cost, and setting-change terms. `irq_sim_episode_step` returns delta reward for the step. `irq_sim_metrics` returns cumulative episode metrics.

## Lifecycle

1. Allocate one `irq_sim_t` per environment instance.
2. Fill `irq_sim_config_t`, then call `irq_sim_validate_config` or `irq_sim_reset`.
3. For each step, pass one `irq_action_t` to `irq_sim_episode_step`.
4. Read `done` from the step result. The simulator sets done when `tick >= episode_ticks`.
5. Reset before reusing an instance for a new episode.

The C core owns no heap memory and uses no global mutable simulator state. `irq_sim_t` contains fixed-size ring and histogram storage, so wrappers should store it by pointer or embedded environment state rather than copying it per step.

For fair benchmark comparison, use the same control interval for heuristic policies, bandit baselines, and learned policies. Per-tick stepping is valid, but it has more control authority and should be labeled separately.

## Build Assumptions

- The C core has no external dependencies.
- The core can be compiled as C11.
- Ocean integration may need a separate generated binding layer.

## Mac CPU Path Notes

The standalone path builds with the system C compiler on macOS. Address sanitizer is optional through `make asan`.

## CUDA Or Remote Path Notes

No CUDA path exists yet. A future GPU path should keep the scalar C simulator as the reference implementation.

## Open Questions

- Exact Ocean environment struct layout.
- Preferred observation storage type and ownership, likely `float32` in Python even though C exports `double`.
- Batch stepping API shape.
- Build system conventions for mixed C and Python packaging.
