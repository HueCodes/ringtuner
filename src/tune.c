#include "irq_sim.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_GRID 64u

typedef enum {
    TUNE_MODE_PER_PROFILE = 0,
    TUNE_MODE_MEAN,
    TUNE_MODE_WORST,
} tune_mode_t;

typedef enum {
    TRAFFIC_SELECT_ALL = 0,
    TRAFFIC_SELECT_SCENARIO,
    TRAFFIC_SELECT_ONE,
} traffic_select_mode_t;

typedef struct {
    uint32_t values[MAX_GRID];
    size_t count;
} grid_t;

typedef struct {
    tune_mode_t mode;
    uint64_t train_seeds;
    uint64_t eval_seeds;
    traffic_select_mode_t traffic_mode;
    irq_traffic_profile_t traffic;
} tune_run_options_t;

typedef irq_tuning_result_t eval_result_t;

static void usage(const char *prog) {
    printf("usage: %s [--mode per-profile|mean|worst] [--train-seeds N] [--eval-seeds N]\n"
           "          [--ticks N] [--scenario NAME|all] [--csv results/file.csv]\n"
           "          [--traffic all|scenario|NAME|INDEX]\n"
           "          [--packet-grid csv] [--timer-grid csv] [--help]\n",
           prog);
}

static void list_scenarios(FILE *out) {
    for (size_t i = 0u; i < irq_scenario_count(); i++) {
        const irq_scenario_t *s = irq_scenario_by_index(i);
        fprintf(out, "%s\n", s->name);
    }
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

static bool parse_mode(const char *s, tune_mode_t *out) {
    if (strcmp(s, "per-profile") == 0) {
        *out = TUNE_MODE_PER_PROFILE;
        return true;
    }
    if (strcmp(s, "mean") == 0) {
        *out = TUNE_MODE_MEAN;
        return true;
    }
    if (strcmp(s, "worst") == 0) {
        *out = TUNE_MODE_WORST;
        return true;
    }
    return false;
}

static bool parse_traffic_arg(const char *s, irq_traffic_profile_t *out) {
    uint64_t idx = 0u;
    if (parse_u64(s, &idx) && idx < IRQ_TRAFFIC_COUNT) {
        *out = (irq_traffic_profile_t)idx;
        return true;
    }
    for (int i = 0; i < (int)IRQ_TRAFFIC_COUNT; i++) {
        if (strcmp(s, irq_traffic_name((irq_traffic_profile_t)i)) == 0) {
            *out = (irq_traffic_profile_t)i;
            return true;
        }
    }
    return false;
}

static const char *mode_name(tune_mode_t mode) {
    switch (mode) {
    case TUNE_MODE_PER_PROFILE:
        return "per-profile";
    case TUNE_MODE_MEAN:
        return "mean";
    case TUNE_MODE_WORST:
        return "worst";
    }
    return "invalid";
}

static const char *traffic_scope_name(traffic_select_mode_t mode, irq_traffic_profile_t traffic) {
    switch (mode) {
    case TRAFFIC_SELECT_ALL:
        return "all";
    case TRAFFIC_SELECT_SCENARIO:
        return "scenario";
    case TRAFFIC_SELECT_ONE:
        return irq_traffic_name(traffic);
    }
    return "invalid";
}

static bool csv_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
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

static void csv_candidate(FILE *csv,
                          const char *scenario,
                          const char *scope,
                          const char *traffic,
                          uint32_t packet_threshold,
                          uint32_t timer_threshold,
                          const char *split,
                          const eval_result_t *r,
                          double mean_reward,
                          double worst_reward) {
    if (csv == NULL) {
        return;
    }
    fprintf(csv,
            "%s,%s,%s,%u,%u,%s,%.9f,%.9f,%.9f,%.3f,%.3f,%.6f,%.6f\n",
            scenario,
            scope,
            traffic,
            packet_threshold,
            timer_threshold,
            split,
            r == NULL ? mean_reward : r->reward,
            mean_reward,
            worst_reward,
            r == NULL ? 0.0 : r->p99_latency,
            r == NULL ? 0.0 : r->interrupts,
            r == NULL ? 0.0 : r->delivered_ratio,
            r == NULL ? 0.0 : r->drop_ratio);
}

static bool run_tuning(const char *run_scenario_name,
                       irq_sim_config_t cfg,
                       const grid_t *packet_grid,
                       const grid_t *timer_grid,
                       const tune_run_options_t *opts,
                       const bool enabled[IRQ_TRAFFIC_COUNT],
                       FILE *csv) {
    char err[128];
    if (!irq_sim_validate_config(&cfg, err, sizeof(err))) {
        fprintf(stderr, "invalid config for %s: %s\n", run_scenario_name, err);
        return false;
    }

    printf("offline threshold tuning, scenario=%s, traffic=%s, mode=%s, ticks=%llu, train_seeds=%llu, eval_seeds=%llu\n",
           run_scenario_name,
           traffic_scope_name(opts->traffic_mode, opts->traffic),
           mode_name(opts->mode),
           (unsigned long long)cfg.episode_ticks,
           (unsigned long long)opts->train_seeds,
           (unsigned long long)opts->eval_seeds);
    printf("grid size: %zu packet thresholds x %zu timer thresholds = %zu candidates\n",
           packet_grid->count,
           timer_grid->count,
           packet_grid->count * timer_grid->count);

    eval_result_t best_profile[IRQ_TRAFFIC_COUNT];
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        memset(&best_profile[t], 0, sizeof(best_profile[t]));
        best_profile[t].reward = -DBL_MAX;
    }
    eval_result_t best_mean = {0};
    eval_result_t best_worst = {0};
    double best_mean_reward = -DBL_MAX;
    double best_mean_worst = -DBL_MAX;
    double best_worst_mean = -DBL_MAX;
    double best_worst_reward = -DBL_MAX;

    eval_result_t per_profile[IRQ_TRAFFIC_COUNT];
    for (size_t p = 0u; p < packet_grid->count; p++) {
        for (size_t q = 0u; q < timer_grid->count; q++) {
            double mean_reward = 0.0;
            double worst_reward = 0.0;
            if (!irq_tune_eval_profiles(cfg,
                                        enabled,
                                        packet_grid->values[p],
                                        timer_grid->values[q],
                                        1u,
                                        opts->train_seeds,
                                        per_profile,
                                        &mean_reward,
                                        &worst_reward)) {
                fprintf(stderr,
                        "candidate failed for %s packet=%u timer=%u\n",
                        run_scenario_name,
                        packet_grid->values[p],
                        timer_grid->values[q]);
                return false;
            }
            for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
                if (!enabled[t]) {
                    continue;
                }
                csv_candidate(csv,
                              run_scenario_name,
                              "candidate",
                              irq_traffic_name((irq_traffic_profile_t)t),
                              packet_grid->values[p],
                              timer_grid->values[q],
                              "train",
                              &per_profile[t],
                              mean_reward,
                              worst_reward);
                if (per_profile[t].reward > best_profile[t].reward) {
                    best_profile[t] = per_profile[t];
                }
            }
            if (mean_reward > best_mean_reward) {
                best_mean_reward = mean_reward;
                best_mean_worst = worst_reward;
                best_mean.packet_threshold = packet_grid->values[p];
                best_mean.timer_threshold = timer_grid->values[q];
                best_mean.reward = mean_reward;
            }
            if (worst_reward > best_worst_reward) {
                best_worst_reward = worst_reward;
                best_worst_mean = mean_reward;
                best_worst.packet_threshold = packet_grid->values[p];
                best_worst.timer_threshold = timer_grid->values[q];
                best_worst.reward = worst_reward;
            }
        }
    }

    printf("\n%-16s %8s %8s %12s %12s %9s %10s %9s\n",
           "selection",
           "pkt",
           "timer",
           "train",
           "eval",
           "p99_eval",
           "intr_eval",
           "drop_eval");

    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        if (!enabled[t]) {
            continue;
        }
        eval_result_t eval;
        if (!irq_tune_eval_profile(cfg,
                                   (irq_traffic_profile_t)t,
                                   best_profile[t].packet_threshold,
                                   best_profile[t].timer_threshold,
                                   10001u,
                                   opts->eval_seeds,
                                   &eval)) {
            fprintf(stderr, "eval failed for %s/%s\n", run_scenario_name, irq_traffic_name((irq_traffic_profile_t)t));
            return false;
        }
        csv_candidate(csv,
                      run_scenario_name,
                      "best_per_profile",
                      irq_traffic_name((irq_traffic_profile_t)t),
                      best_profile[t].packet_threshold,
                      best_profile[t].timer_threshold,
                      "eval",
                      &eval,
                      eval.reward,
                      eval.reward);
        printf("%-16s %8u %8u %12.6f %12.6f %9.3f %10.3f %9.6f\n",
               irq_traffic_name((irq_traffic_profile_t)t),
               best_profile[t].packet_threshold,
               best_profile[t].timer_threshold,
               best_profile[t].reward,
               eval.reward,
               eval.p99_latency,
               eval.interrupts,
               eval.drop_ratio);
    }

    double eval_mean = 0.0;
    double eval_worst = 0.0;
    if (!irq_tune_eval_profiles(cfg,
                                enabled,
                                best_mean.packet_threshold,
                                best_mean.timer_threshold,
                                10001u,
                                opts->eval_seeds,
                                per_profile,
                                &eval_mean,
                                &eval_worst)) {
        fprintf(stderr, "global mean eval failed for %s\n", run_scenario_name);
        return false;
    }
    csv_candidate(csv,
                  run_scenario_name,
                  "best_global_mean",
                  traffic_scope_name(opts->traffic_mode, opts->traffic),
                  best_mean.packet_threshold,
                  best_mean.timer_threshold,
                  "eval",
                  NULL,
                  eval_mean,
                  eval_worst);
    printf("%-16s %8u %8u %12.6f %12.6f %9s %10s %9s\n",
           "global_mean",
           best_mean.packet_threshold,
           best_mean.timer_threshold,
           best_mean_reward,
           eval_mean,
           "-",
           "-",
           "-");

    if (!irq_tune_eval_profiles(cfg,
                                enabled,
                                best_worst.packet_threshold,
                                best_worst.timer_threshold,
                                10001u,
                                opts->eval_seeds,
                                per_profile,
                                &eval_mean,
                                &eval_worst)) {
        fprintf(stderr, "global worst eval failed for %s\n", run_scenario_name);
        return false;
    }
    csv_candidate(csv,
                  run_scenario_name,
                  "best_global_worst",
                  traffic_scope_name(opts->traffic_mode, opts->traffic),
                  best_worst.packet_threshold,
                  best_worst.timer_threshold,
                  "eval",
                  NULL,
                  eval_mean,
                  eval_worst);
    printf("%-16s %8u %8u %12.6f %12.6f %9s %10s %9s\n",
           "global_worst",
           best_worst.packet_threshold,
           best_worst.timer_threshold,
           best_worst_reward,
           eval_worst,
           "-",
           "-",
           "-");

    printf("\ntrain global mean: packet=%u timer=%u mean=%.6f worst=%.6f\n",
           best_mean.packet_threshold,
           best_mean.timer_threshold,
           best_mean_reward,
           best_mean_worst);
    printf("train global worst: packet=%u timer=%u mean=%.6f worst=%.6f\n\n",
           best_worst.packet_threshold,
           best_worst.timer_threshold,
           best_worst_mean,
           best_worst_reward);

    return true;
}

static bool scenario_grids(const irq_scenario_t *scenario,
                           const grid_t *base_packet_grid,
                           const grid_t *base_timer_grid,
                           bool custom_packet_grid,
                           bool custom_timer_grid,
                           grid_t *packet_grid,
                           grid_t *timer_grid) {
    *packet_grid = *base_packet_grid;
    *timer_grid = *base_timer_grid;
    if (scenario == NULL) {
        return true;
    }
    if (!custom_packet_grid &&
        !filter_grid_range(packet_grid, scenario->min_packet_threshold, scenario->max_packet_threshold)) {
        fprintf(stderr, "scenario %s packet threshold range has no candidates\n", scenario->name);
        return false;
    }
    if (!custom_timer_grid && !filter_grid_range(timer_grid, scenario->min_timer_threshold, scenario->max_timer_threshold)) {
        fprintf(stderr, "scenario %s timer threshold range has no candidates\n", scenario->name);
        return false;
    }
    return true;
}

static bool select_traffic(const tune_run_options_t *opts, const irq_scenario_t *scenario, bool enabled[IRQ_TRAFFIC_COUNT]) {
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        enabled[t] = false;
    }
    switch (opts->traffic_mode) {
    case TRAFFIC_SELECT_ALL:
        for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
            enabled[t] = true;
        }
        return true;
    case TRAFFIC_SELECT_SCENARIO:
        if (scenario == NULL) {
            fprintf(stderr, "--traffic scenario requires --scenario NAME or --scenario all\n");
            return false;
        }
        enabled[scenario->cfg.traffic_profile] = true;
        return true;
    case TRAFFIC_SELECT_ONE:
        enabled[opts->traffic] = true;
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    tune_mode_t mode = TUNE_MODE_PER_PROFILE;
    uint64_t train_seeds = 16u;
    uint64_t eval_seeds = 16u;
    traffic_select_mode_t traffic_mode = TRAFFIC_SELECT_ALL;
    irq_traffic_profile_t selected_traffic = IRQ_TRAFFIC_STEADY_LOW;
    const char *scenario_name = NULL;
    bool all_scenarios = false;
    const char *csv_path = NULL;
    grid_t base_packet_grid = {{1u, 2u, 4u, 8u, 12u, 16u, 32u, 64u}, 8u};
    grid_t base_timer_grid = {{0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}, 9u};
    bool custom_packet_grid = false;
    bool custom_timer_grid = false;
    uint64_t episode_ticks_override = 0u;
    bool custom_ticks = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (!parse_mode(argv[++i], &mode)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &episode_ticks_override)) {
                usage(argv[0]);
                return 2;
            }
            custom_ticks = true;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario_name = argv[++i];
            all_scenarios = strcmp(scenario_name, "all") == 0;
            if (!all_scenarios && irq_scenario_by_name(scenario_name) == NULL) {
                fprintf(stderr, "invalid scenario: %s\nvalid scenarios:\n", scenario_name);
                list_scenarios(stderr);
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
        } else if (strcmp(argv[i], "--traffic") == 0 && i + 1 < argc) {
            const char *traffic_arg = argv[++i];
            if (strcmp(traffic_arg, "all") == 0) {
                traffic_mode = TRAFFIC_SELECT_ALL;
            } else if (strcmp(traffic_arg, "scenario") == 0) {
                traffic_mode = TRAFFIC_SELECT_SCENARIO;
            } else if (parse_traffic_arg(traffic_arg, &selected_traffic)) {
                traffic_mode = TRAFFIC_SELECT_ONE;
            } else {
                fprintf(stderr, "invalid traffic: %s\n", traffic_arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--packet-grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], &base_packet_grid)) {
                usage(argv[0]);
                return 2;
            }
            custom_packet_grid = true;
        } else if (strcmp(argv[i], "--timer-grid") == 0 && i + 1 < argc) {
            if (!parse_grid(argv[++i], &base_timer_grid)) {
                usage(argv[0]);
                return 2;
            }
            custom_timer_grid = true;
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
            if (!csv_path_allowed(csv_path)) {
                fprintf(stderr, "csv path must be under results/ and must not contain ..\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &train_seeds) || train_seeds == 0u || train_seeds > 512u) {
                usage(argv[0]);
                return 2;
            }
            eval_seeds = train_seeds;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (traffic_mode == TRAFFIC_SELECT_SCENARIO && scenario_name == NULL) {
        fprintf(stderr, "--traffic scenario requires --scenario NAME or --scenario all\n");
        return 2;
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
        fprintf(csv, "scenario,scope,traffic,packet_threshold,timer_threshold,split,reward,mean_reward,worst_reward,p99,interrupts,delivered_ratio,drop_ratio\n");
    }

    const tune_run_options_t opts = {mode, train_seeds, eval_seeds, traffic_mode, selected_traffic};
    bool ok = true;
    if (all_scenarios) {
        for (size_t i = 0u; i < irq_scenario_count(); i++) {
            const irq_scenario_t *scenario = irq_scenario_by_index(i);
            grid_t packet_grid;
            grid_t timer_grid;
            bool enabled[IRQ_TRAFFIC_COUNT];
            irq_sim_config_t run_cfg = scenario->cfg;
            if (custom_ticks) {
                run_cfg.episode_ticks = episode_ticks_override;
            }
            if (!scenario_grids(scenario,
                                &base_packet_grid,
                                &base_timer_grid,
                                custom_packet_grid,
                                custom_timer_grid,
                                &packet_grid,
                                &timer_grid) ||
                !select_traffic(&opts, scenario, enabled) ||
                !run_tuning(scenario->name, run_cfg, &packet_grid, &timer_grid, &opts, enabled, csv)) {
                ok = false;
                break;
            }
        }
    } else {
        const irq_scenario_t *scenario = scenario_name == NULL ? NULL : irq_scenario_by_name(scenario_name);
        grid_t packet_grid;
        grid_t timer_grid;
        bool enabled[IRQ_TRAFFIC_COUNT];
        irq_sim_config_t run_cfg = scenario == NULL ? irq_sim_default_config() : scenario->cfg;
        const char *run_scenario_name = scenario == NULL ? "default" : scenario->name;
        if (custom_ticks) {
            run_cfg.episode_ticks = episode_ticks_override;
        }
        ok = scenario_grids(scenario,
                            &base_packet_grid,
                            &base_timer_grid,
                            custom_packet_grid,
                            custom_timer_grid,
                            &packet_grid,
                            &timer_grid) &&
             select_traffic(&opts, scenario, enabled) &&
             run_tuning(run_scenario_name, run_cfg, &packet_grid, &timer_grid, &opts, enabled, csv);
    }

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return ok ? 0 : 1;
}
