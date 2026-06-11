#include "irq_sim.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_GRID 64u

typedef struct {
    uint32_t values[MAX_GRID];
    size_t count;
} grid_t;

typedef struct {
    double reward;
    double p99_latency;
    double interrupts;
    double delivered_ratio;
    double drop_ratio;
} avg_metrics_t;

static void usage(const char *prog) {
    printf("usage: %s [--scenario NAME|all] [--train-seeds N] [--eval-seeds N]\n"
           "          [--packet-grid csv] [--timer-grid csv] [--csv results/file.csv] [--help]\n",
           prog);
}

static bool parse_u64(const char *s, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    const unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static bool parse_u32(const char *s, uint32_t *out) {
    uint64_t v = 0u;
    if (!parse_u64(s, &v) || v > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_grid(const char *s, grid_t *grid) {
    char buf[512];
    if (s == NULL || grid == NULL || strlen(s) >= sizeof(buf)) {
        return false;
    }
    snprintf(buf, sizeof(buf), "%s", s);
    grid->count = 0u;
    char *tok = strtok(buf, ",");
    while (tok != NULL) {
        if (grid->count >= MAX_GRID || !parse_u32(tok, &grid->values[grid->count])) {
            return false;
        }
        grid->count++;
        tok = strtok(NULL, ",");
    }
    return grid->count > 0u;
}

static bool filter_grid_range(grid_t *grid, uint32_t min_value, uint32_t max_value) {
    size_t out = 0u;
    if (grid == NULL || min_value > max_value) {
        return false;
    }
    for (size_t i = 0u; i < grid->count; i++) {
        if (grid->values[i] >= min_value && grid->values[i] <= max_value) {
            grid->values[out++] = grid->values[i];
        }
    }
    grid->count = out;
    return grid->count > 0u;
}

static bool csv_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
}

static bool run_direct(const irq_sim_config_t *cfg, irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (!irq_sim_reset(&sim, cfg)) {
        return false;
    }
    while (!sim.done) {
        (void)irq_sim_step(&sim);
    }
    *out = irq_sim_metrics(&sim);
    return true;
}

static bool average_baseline(irq_sim_config_t cfg, irq_baseline_policy_t policy, uint64_t seed_start, uint64_t seed_count, avg_metrics_t *out) {
    if (out == NULL || seed_count == 0u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    for (uint64_t i = 0u; i < seed_count; i++) {
        irq_sim_metrics_t m;
        cfg.seed = seed_start + i;
        if (!irq_sim_run_baseline(&cfg, policy, &m)) {
            return false;
        }
        out->reward += m.reward;
        out->p99_latency += m.p99_latency;
        out->interrupts += (double)m.interrupts;
        out->delivered_ratio += m.delivered_ratio;
        out->drop_ratio += m.drop_ratio;
    }
    out->reward /= (double)seed_count;
    out->p99_latency /= (double)seed_count;
    out->interrupts /= (double)seed_count;
    out->delivered_ratio /= (double)seed_count;
    out->drop_ratio /= (double)seed_count;
    return true;
}

static bool average_direct(irq_sim_config_t cfg, uint64_t seed_start, uint64_t seed_count, avg_metrics_t *out) {
    if (out == NULL || seed_count == 0u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    for (uint64_t i = 0u; i < seed_count; i++) {
        irq_sim_metrics_t m;
        cfg.seed = seed_start + i;
        if (!run_direct(&cfg, &m)) {
            return false;
        }
        out->reward += m.reward;
        out->p99_latency += m.p99_latency;
        out->interrupts += (double)m.interrupts;
        out->delivered_ratio += m.delivered_ratio;
        out->drop_ratio += m.drop_ratio;
    }
    out->reward /= (double)seed_count;
    out->p99_latency /= (double)seed_count;
    out->interrupts /= (double)seed_count;
    out->delivered_ratio /= (double)seed_count;
    out->drop_ratio /= (double)seed_count;
    return true;
}

static bool tune_scenario_traffic(const irq_scenario_t *scenario,
                                  const grid_t *base_packet_grid,
                                  const grid_t *base_timer_grid,
                                  uint64_t train_seeds,
                                  irq_tuning_result_t *best) {
    grid_t packet_grid = *base_packet_grid;
    grid_t timer_grid = *base_timer_grid;
    if (scenario == NULL || best == NULL ||
        !filter_grid_range(&packet_grid, scenario->min_packet_threshold, scenario->max_packet_threshold) ||
        !filter_grid_range(&timer_grid, scenario->min_timer_threshold, scenario->max_timer_threshold)) {
        return false;
    }
    memset(best, 0, sizeof(*best));
    best->reward = -DBL_MAX;
    for (size_t p = 0u; p < packet_grid.count; p++) {
        for (size_t q = 0u; q < timer_grid.count; q++) {
            irq_tuning_result_t candidate;
            if (!irq_tune_eval_profile(scenario->cfg,
                                       scenario->cfg.traffic_profile,
                                       packet_grid.values[p],
                                       timer_grid.values[q],
                                       1u,
                                       train_seeds,
                                       &candidate)) {
                return false;
            }
            if (candidate.reward > best->reward) {
                *best = candidate;
            }
        }
    }
    return true;
}

static void csv_row(FILE *csv,
                    const char *scenario,
                    const char *policy,
                    uint32_t packet_threshold,
                    uint32_t timer_threshold,
                    const avg_metrics_t *metrics,
                    double reward_delta_vs_fixed_balanced) {
    if (csv == NULL) {
        return;
    }
    fprintf(csv,
            "%s,%s,%u,%u,%.9f,%.3f,%.3f,%.6f,%.6f,%.9f\n",
            scenario,
            policy,
            packet_threshold,
            timer_threshold,
            metrics->reward,
            metrics->p99_latency,
            metrics->interrupts,
            metrics->delivered_ratio,
            metrics->drop_ratio,
            reward_delta_vs_fixed_balanced);
}

static bool compare_scenario(const irq_scenario_t *scenario,
                             const grid_t *packet_grid,
                             const grid_t *timer_grid,
                             uint64_t train_seeds,
                             uint64_t eval_seeds,
                             FILE *csv) {
    static const irq_baseline_policy_t baselines[] = {
        IRQ_POLICY_FIXED_LOW_LATENCY,
        IRQ_POLICY_FIXED_BALANCED,
        IRQ_POLICY_FIXED_THROUGHPUT,
        IRQ_POLICY_SIMPLE_ADAPTIVE,
        IRQ_POLICY_ADAPTIVE_BANDIT,
        IRQ_POLICY_NAPI_POLLING,
    };
    irq_tuning_result_t tuned;
    avg_metrics_t fixed_balanced;
    if (scenario == NULL ||
        !tune_scenario_traffic(scenario, packet_grid, timer_grid, train_seeds, &tuned) ||
        !average_baseline(scenario->cfg, IRQ_POLICY_FIXED_BALANCED, 10001u, eval_seeds, &fixed_balanced)) {
        return false;
    }

    printf("\n%s traffic=%s tuned packet=%u timer=%u train_reward=%.6f\n",
           scenario->name,
           irq_traffic_name(scenario->cfg.traffic_profile),
           tuned.packet_threshold,
           tuned.timer_threshold,
           tuned.reward);
    printf("%-22s %8s %8s %12s %9s %10s %9s %11s\n",
           "policy",
           "pkt",
           "timer",
           "eval_reward",
           "p99",
           "interrupts",
           "drop",
           "delta");

    for (size_t i = 0u; i < sizeof(baselines) / sizeof(baselines[0]); i++) {
        avg_metrics_t m;
        if (!average_baseline(scenario->cfg, baselines[i], 10001u, eval_seeds, &m)) {
            return false;
        }
        const double delta = m.reward - fixed_balanced.reward;
        printf("%-22s %8s %8s %12.6f %9.3f %10.3f %9.6f %11.6f\n",
               irq_policy_name(baselines[i]),
               "-",
               "-",
               m.reward,
               m.p99_latency,
               m.interrupts,
               m.drop_ratio,
               delta);
        csv_row(csv, scenario->name, irq_policy_name(baselines[i]), 0u, 0u, &m, delta);
    }

    irq_sim_config_t tuned_cfg = scenario->cfg;
    tuned_cfg.packet_threshold = tuned.packet_threshold;
    tuned_cfg.timer_threshold = tuned.timer_threshold;
    avg_metrics_t tuned_metrics;
    if (!average_direct(tuned_cfg, 10001u, eval_seeds, &tuned_metrics)) {
        return false;
    }
    const double tuned_delta = tuned_metrics.reward - fixed_balanced.reward;
    printf("%-22s %8u %8u %12.6f %9.3f %10.3f %9.6f %11.6f\n",
           "tuned_direct",
           tuned.packet_threshold,
           tuned.timer_threshold,
           tuned_metrics.reward,
           tuned_metrics.p99_latency,
           tuned_metrics.interrupts,
           tuned_metrics.drop_ratio,
           tuned_delta);
    csv_row(csv, scenario->name, "tuned_direct", tuned.packet_threshold, tuned.timer_threshold, &tuned_metrics, tuned_delta);
    return true;
}

int main(int argc, char **argv) {
    const char *scenario_name = "all";
    const char *csv_path = NULL;
    uint64_t train_seeds = 16u;
    uint64_t eval_seeds = 16u;
    grid_t packet_grid = {{1u, 2u, 4u, 8u, 12u, 16u, 32u, 64u}, 8u};
    grid_t timer_grid = {{0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}, 9u};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario_name = argv[++i];
            if (strcmp(scenario_name, "all") != 0 && irq_scenario_by_name(scenario_name) == NULL) {
                fprintf(stderr, "invalid scenario: %s\n", scenario_name);
                return 2;
            }
        } else if (strcmp(argv[i], "--train-seeds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &train_seeds) || train_seeds == 0u || train_seeds > 512u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--eval-seeds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &eval_seeds) || eval_seeds == 0u || eval_seeds > 512u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--packet-grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], &packet_grid)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--timer-grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], &timer_grid)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
            if (!csv_path_allowed(csv_path)) {
                fprintf(stderr, "csv path must be under results/ and must not contain ..\n");
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    FILE *csv = NULL;
    if (csv_path != NULL) {
        if (mkdir("results", 0777) != 0 && errno != EEXIST) {
            perror("mkdir results");
            return 1;
        }
        csv = fopen(csv_path, "w");
        if (csv == NULL) {
            perror("fopen csv");
            return 1;
        }
        fprintf(csv, "scenario,policy,packet_threshold,timer_threshold,reward,p99,interrupts,delivered_ratio,drop_ratio,reward_delta_vs_fixed_balanced\n");
    }

    printf("scenario traffic comparison, train_seeds=%llu, eval_seeds=%llu\n",
           (unsigned long long)train_seeds,
           (unsigned long long)eval_seeds);

    bool ok = true;
    if (strcmp(scenario_name, "all") == 0) {
        for (size_t i = 0u; i < irq_scenario_count(); i++) {
            if (!compare_scenario(irq_scenario_by_index(i), &packet_grid, &timer_grid, train_seeds, eval_seeds, csv)) {
                ok = false;
                break;
            }
        }
    } else {
        ok = compare_scenario(irq_scenario_by_name(scenario_name), &packet_grid, &timer_grid, train_seeds, eval_seeds, csv);
    }

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return ok ? 0 : 1;
}
