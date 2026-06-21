# NAPI-Style Polling Baseline

RingTuner includes `napi_polling` as a practical baseline between interrupt-per-service policies and pure fixed coalescing.

Model behavior:

- The policy starts with balanced coalescing thresholds.
- When an interrupt fires, it drains up to `service_budget` packets and enters poll mode.
- While poll mode is active, each later tick drains up to `service_budget` packets without counting another hardware interrupt.
- Poll mode exits after two idle polls.
- Synthetic traffic and trace replay use the same polling path.

What this captures:

- Hardware interrupts can be reduced during bursts.
- Service is still bounded by `service_budget` per tick.
- Queue pressure can continue across ticks without charging every poll as an interrupt.

What it does not capture:

- Linux NAPI budget accounting.
- Softirq scheduling details.
- Driver-specific poll loops.
- IRQ masking, affinity, or cache effects.

The baseline is intended for policy comparison inside this simulator. It is not a kernel behavior claim.
