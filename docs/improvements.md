# Offline Improvement Backlog

These are improvements that can be added without network access, dependency installs, public actions, pushes, or PRs.

## Implemented In This Pass

1. Add an offline tuning harness.
   - Build a C executable that grid-searches packet and timer thresholds.
   - Average results across deterministic seeds.
   - Save aggregated CSV under `results/`.

2. Add a `make tune` target.
   - Keep tuning as a first-class local command.
   - Use the existing simulator core rather than duplicating mechanics.

3. Add benchmark CLI controls.
   - Allow `--ring`, `--budget`, `--profile`, and `--policy`.
   - Keep default behavior as all profiles across all baselines.

4. Add threshold override controls.
   - Allow `--packet-threshold` and `--timer-threshold` for direct fixed-setting experiments.
   - Validate all numeric inputs through the existing config validator.

5. Expand benchmark CSV output.
   - Include offered packets, final queue depth, and average queue depth.
   - Keep generated CSVs under `results/`.

6. Add tuning result documentation.
   - Record the local tuning run, command, and best thresholds.
   - Avoid overstating the result as RL training.

7. Add config validation tests.
   - Invalid ring capacity.
   - Invalid service budget.
   - Invalid thresholds.
   - Invalid episode length.

8. Strengthen no-coalescing tests.
   - Check that no-coalescing emits one interrupt per delivered packet in benchmark mode.

9. Strengthen reward tests.
   - Check unresolved queue depth lowers reward.
   - Check step reward remains finite through repeated actions.

10. Add benchmark documentation.
    - Document benchmark and tuning commands.
    - Document CSV schemas.

11. Add local audit notes for the second pass.
    - Summarize what changed.
    - Record remaining limitations.

12. Keep generated artifacts ignored.
    - `results/` remains ignored.
    - Build outputs remain ignored.

13. Keep all work standalone.
    - No PufferLib clone or install.
    - No dependency manager.
    - No public action.

14. Add scenario-scoped tuning.
    - `build/tune --scenario NAME` uses the scenario config and threshold bounds.
    - Default tuning grids are filtered to the scenario range unless explicit grids are provided.
    - Tuning CSV rows now include a scenario column.

15. Add scenario tuning coverage.
    - Validate every scenario tuning range.
    - Run min and max scenario tuning points through the core evaluator.
    - Add `make tune-scenario` and include a small-ring tuning artifact in `make report`.

16. Add all-scenario tuning.
    - `build/tune --scenario all` tunes each scenario independently.
    - `make tune-scenarios` writes `results/tuning-scenarios.csv`.
    - `make report` now includes the full scenario tuning matrix.

17. Add traffic-selective tuning.
    - `build/tune --traffic NAME` tunes one traffic profile.
    - `build/tune --scenario all --traffic scenario` tunes each scenario only against its declared traffic profile.
    - `make tune-scenario-traffic` writes `results/tuning-scenario-traffic.csv`.

18. Add tuned baseline comparison.
    - `build/compare` tunes scenario-declared traffic and evaluates held-out seeds.
    - `make compare` writes `results/comparison.csv`.
    - `make report` now includes the comparison artifact.

## Still Deferred

1. Actual RL training with PPO or another learner.
   - Needs a training framework or a custom learner.
   - Deferred because dependency installs and network access are disallowed.

2. PufferLib Ocean integration.
   - Deferred until standalone behavior is stable.

3. Real NIC trace replay.
   - Needs external trace data or generated fixtures.

4. Multi-queue simulation.
   - Useful, but larger than the current one-queue baseline.

5. Hardware calibration.
   - Needs measurements outside this local sandbox.
