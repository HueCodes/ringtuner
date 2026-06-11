# Security Notes

- CLI inputs are treated as untrusted.
- Numeric ranges are validated for ticks, seed, profile, baseline, thresholds, ring size, and service budget.
- Episode length and timer thresholds are bounded below the latency histogram range so valid percentile outputs are not clipped.
- Ring indexing wraps through bounded modular arithmetic.
- Counters use fixed-width integer types.
- Latency sums saturate on overflow to keep metrics finite.
- Generated CSV output is restricted to paths under `results/`.
- The simulator does not execute generated files.
- No network access, dependency download, or external data is required.
- Future JSON, TOML, or YAML support should use maintained parsers.
- Random generation is deterministic and local. It makes no cryptographic claim.
