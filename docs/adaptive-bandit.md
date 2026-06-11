# Adaptive Bandit Baseline

RingTuner includes a tiny dependency-free adaptive controller named `adaptive_bandit`.

It is not PPO, deep RL, or online policy-gradient training. It is an epsilon-greedy bandit over the simulator's discrete action set:

- low latency
- balanced low
- balanced high
- throughput
- bulk

Behavior:

- control window: 64 ticks
- exploration: deterministic epsilon-greedy with epsilon 1/8
- reward signal: change in simulator reward over the control window
- state: per-action average window reward and sample count
- seed: derived from the simulator seed

Why it exists:

- Provides a small adaptive baseline with no dependencies.
- Exercises the RL-facing action path over full episodes.
- Gives later RL work a floor that is stronger than a single fixed policy but weaker than tuned per-profile thresholds.

Limitations:

- It ignores observations except through reward feedback.
- It does not learn a state-conditioned policy.
- It can over-explore on short episodes.
- It is not expected to beat grid-search tuning on deterministic profiles.
