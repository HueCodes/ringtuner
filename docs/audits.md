# Audits

## Milestone 0: Scaffold

- Files are under `/Users/hugh/Dev/projects/ringtuner`.
- No network access, dependency install, public action, push, or PR was used.
- Generated `build/` and `results/` paths are ignored.

## Milestone 1: Spec

- Realism: version 1 is intentionally one queue, discrete ticks, and simplified coalescing. Limitations are explicit in `docs/design.md`.
- Implementability: all required state maps to fixed-size C structs and deterministic traffic generators.
- Overcomplication: packet sizes, multi-queue, DMA, PCIe, and hardware calibration are deferred. NAPI-style polling is modeled only as a simplified scheduling baseline.

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

## Resume-Grade Audit

Simulator correctness risks:

- The model is still single-queue and tick-based. It is useful for control experiments, not NIC fidelity claims.
- CPU service is compressed into one tick. `service_budget` bounds packet delivery per interrupt or polling tick, but the model does not simulate explicit service time or IRQ affinity.
- `no_coalescing_oracle` is intentionally idealized and must remain labeled as an oracle in output and docs.
- Traffic profiles are deterministic and synthetic. They exercise control behavior, but they are not trace-derived.

Test coverage gaps:

- Tests now cover invalid actions, invalid configs, max ring capacity, ring capacity 1, service budget 1, timer threshold 0, packet threshold equal to ring capacity, one-tick episodes, metrics accounting, scenario lookup, reward components, and oracle versus CPU-limited no coalescing.
- Tuning candidate evaluation now has direct deterministic tests through the core API.
- A zero-arrival traffic profile now covers no-arrivals accounting and neutral reward behavior.
- Remaining gap: full CLI grid traversal is still smoke-tested rather than exhaustively unit-tested.

Reward hacking risks:

- Immediate-service settings still win many profiles because interrupt cost is modest compared with latency penalties.
- Agents can still overfit synthetic profile timing unless evaluated on held-out seeds and scenarios.
- Drops dominate reward under overload, which is intentional, but it can make all realistic policies look similarly negative when service capacity is below offered load.
- Setting-change penalty is simple and may not match a real control-plane cost.

Benchmark fairness issues:

- `no_coalescing_oracle` should not be compared as a feasible production policy.
- `simple_adaptive` updates every 32 ticks while the RL step API can act every tick. Wrappers should choose and document a control interval.
- Scenario results vary ring capacity and service budget, so cross-scenario policy comparisons should report scenario settings.
- Runtime timings are useful for smoke checks, not stable performance benchmarks.

Resume-readiness gaps:

- The project is credible as a C systems simulator with deterministic baselines and offline tuning.
- It should not claim hardware accuracy, production NIC behavior, or completed RL training.
- PufferLib integration is documented but not implemented.
- A learned controller is still future work. The simple adaptive policy is heuristic, and the adaptive bandit is a small baseline controller, not deep RL.

Highest-value next steps:

- Compare the adaptive bandit against tuned thresholds in a small results table.
- Fixed control-interval execution now exists through `irq_sim_run_control_loop`; remaining work is using it consistently in future RL benchmarks.
- Keep one small sample result in docs and keep full generated CSVs ignored under `results/`.

## Scenario Tuning Pass

- Added `build/tune --scenario NAME` so tuning can use a scenario's ring capacity, service budget, traffic profile, episode length, interrupt cost, and declared threshold ranges.
- Added `make tune-scenario` and extended `make report` with `results/tuning-small-ring.csv`.
- Tuning CSV output now includes a `scenario` column.
- Added tests that validate every scenario tuning range and run both endpoint candidates through the evaluator.

## Scenario Matrix Pass

- Refactored the tuning executable around a reusable sweep function.
- Added `build/tune --scenario all` to tune every built-in scenario independently.
- Added `make tune-scenarios` and extended `make report` with `results/tuning-scenarios.csv`.
- Smoke-tested default tuning, single-scenario tuning, and all-scenario tuning.

## Traffic Selection Pass

- Added core support for evaluating a selected subset of traffic profiles.
- Added `build/tune --traffic all|scenario|NAME|INDEX`.
- Added `make tune-scenario-traffic` and extended `make report` with `results/tuning-scenario-traffic.csv`.
- Added tests that compare selected-profile tuning against the existing all-profile evaluator and a direct one-profile run.

## Comparison Pass

- Added `build/compare` to compare tuned scenario thresholds against realistic baselines on held-out seeds.
- Added `make compare` and extended `make report` with `results/comparison.csv`.
- The comparison excludes `no_coalescing_oracle` because it is an idealized reference.
- Smoke-tested one-scenario and all-scenario comparison runs.
