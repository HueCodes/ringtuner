# PufferLib Integration Plan

## Likely Files

- `src/irq_sim.h` and `src/irq_sim.c` remain the portable simulator core.
- A future `pufferlib/` or `envs/` wrapper can own Ocean-specific glue.
- Build integration should compile the C core without changing simulator semantics.

## API Mapping

- Environment reset maps to `irq_sim_reset`.
- Environment step maps to `irq_sim_episode_step`, which applies an action, advances one tick, returns interval observations, and reports delta reward.
- Observation export maps to `irq_sim_observation`.
- Reward maps to `irq_sim_reward`.
- Episode done maps to the configured tick limit.

## Observation, Action, Reward

The current observation vector has nine normalized `double` values. Actions are stable enum values. Step reward is finite and includes delivery, latency, tail latency, unresolved queue depth, drops, interrupt cost, and setting-change terms.

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
- Preferred observation storage type and ownership.
- Batch stepping API shape.
- Build system conventions for mixed C and Python packaging.
