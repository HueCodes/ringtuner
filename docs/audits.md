# Audits

## Milestone 0: Scaffold

- Files are under `/Users/hugh/Dev/projects/ringtuner`.
- No network access, dependency install, public action, push, or PR was used.
- Generated `build/` and `results/` paths are ignored.

## Milestone 1: Spec

- Realism: version 1 is intentionally one queue, discrete ticks, and simplified coalescing. Limitations are explicit in `docs/design.md`.
- Implementability: all required state maps to fixed-size C structs and deterministic traffic generators.
- Overcomplication: packet sizes, multi-queue, NAPI, DMA, PCIe, and hardware calibration are deferred.

## Milestone 2: Simulator

- C correctness: config validation bounds ring size, service budget, thresholds, tick count, traffic profile, and interrupt cost. Ring indices wrap modulo validated capacity. Hot-path packet storage is fixed-size. Counters use fixed-width integers and saturating addition where accumulation can grow.
- Determinism: RNG state is local to `irq_sim_t`, seeded on reset, and no external entropy is used.
- Tests cover seed reproducibility, light-load no loss, overflow drops, interrupt count differences, timer and packet threshold behavior, timer latency effects, adaptive policy changes, ring wraparound accounting, and RL API sanity.

## Milestone 3: Benchmark And Reward

- Metrics support delivered packets, drops, interrupts, interrupt cost, latency percentiles, queue depth, and reward.
- Reward hacking risks are documented in `docs/reward.md`.
- Default `overload_spike` was increased after review so coalesced baselines exercise drop behavior with default settings.
- Final queue depth is penalized so agents cannot hide packets at episode end.

## Milestone 4: RL Interface

- Review found cumulative recent observations and cumulative reward were poor RL semantics.
- Fixed by adding `irq_sim_episode_step`, which clears the recent window, applies one action, advances one tick, returns interval observations, and reports delta reward.
- Invalid actions are rejected. Observation values are finite and bounded.
- Baseline fairness note added: adaptive baseline updates every 32 ticks, while RL step cadence is per tick unless a wrapper chooses a longer control interval.

## Security Review

- CSV output is restricted to `results/` and rejects `..`.
- The CLI creates `results/` locally when CSV output is requested.
- Percentile clipping risk was reduced by bounding valid episode length and timer threshold below the latency histogram range.
- Address sanitizer target exists, but the sanitizer binary did not complete in this sandbox. The normal warning-clean test run passed.

## Subagent Findings Addressed

- RL reward was cumulative: fixed with delta reward in `irq_sim_episode_step`.
- Recent observation fields were cumulative for RL: fixed by clearing recent counters in `irq_sim_episode_step`.
- End-of-episode queued packets were weakly penalized: fixed with unresolved queue penalty.
- No-coalescing baseline batched packets: fixed with an interrupt-per-packet comparison mode.
- Default overload did not overload: fixed by increasing spike arrivals above default service budget.
- Tail latency metrics could clip under valid configs: fixed by tightening timer and episode bounds.

## Second Pass Audit

- Added a C tuning harness and `make tune` for offline threshold search.
- Added benchmark CLI controls for ring capacity, service budget, profile, policy, direct thresholds, and CSV output.
- Expanded CSV output with offered packets, final queue depth, and average queue depth.
- Added config validation tests and stronger no-coalescing assertions.
- Added reward regression coverage for unresolved queued packets.
- Ran `make test` and `make tune` successfully after the changes.
- Ran targeted CLI checks for selected profile/policy, direct threshold mode, and filtered CSV output.
- Remaining limitation: the tuning sweep is grid search, not RL training.
