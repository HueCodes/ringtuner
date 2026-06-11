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

typedef struct {
    uint32_t values[MAX_GRID];
    size_t count;
} grid_t;

typedef irq_tuning_result_t eval_result_t;

static void usage(const char *prog) {
    printf("usage: %s [--mode per-profile|mean|worst] [--train-seeds N] [--eval-seeds N]\n"
           "          [--ticks N] [--csv results/file.csv]\n"
           "          [--packet-grid csv] [--timer-grid csv] [--help]\n",
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

static void csv_candidate(FILE *csv,
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
            "%s,%s,%u,%u,%s,%.9f,%.9f,%.9f,%.3f,%.3f,%.6f,%.6f\n",
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

int main(int argc, char **argv) {
    irq_sim_config_t cfg = irq_sim_default_config();
    tune_mode_t mode = TUNE_MODE_PER_PROFILE;
    uint64_t train_seeds = 16u;
    uint64_t eval_seeds = 16u;
    const char *csv_path = NULL;
    grid_t packet_grid = {{1u, 2u, 4u, 8u, 12u, 16u, 32u, 64u}, 8u};
    grid_t timer_grid = {{0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}, 9u};

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
            if (!parse_u64(argv[++i], &cfg.episode_ticks)) {
                usage(argv[0]);
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

    char err[128];
    if (!irq_sim_validate_config(&cfg, err, sizeof(err))) {
        fprintf(stderr, "invalid config: %s\n", err);
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
        fprintf(csv, "scope,traffic,packet_threshold,timer_threshold,split,reward,mean_reward,worst_reward,p99,interrupts,delivered_ratio,drop_ratio\n");
    }

    printf("offline threshold tuning, mode=%s, ticks=%llu, train_seeds=%llu, eval_seeds=%llu\n",
           mode_name(mode),
           (unsigned long long)cfg.episode_ticks,
           (unsigned long long)train_seeds,
           (unsigned long long)eval_seeds);
    printf("grid size: %zu packet thresholds x %zu timer thresholds = %zu candidates\n",
           packet_grid.count,
           timer_grid.count,
           packet_grid.count * timer_grid.count);

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
    for (size_t p = 0u; p < packet_grid.count; p++) {
        for (size_t q = 0u; q < timer_grid.count; q++) {
            double mean_reward = 0.0;
            double worst_reward = 0.0;
            if (!irq_tune_eval_all_profiles(cfg,
                                            packet_grid.values[p],
                                            timer_grid.values[q],
                                            1u,
                                            train_seeds,
                                            per_profile,
                                            &mean_reward,
                                            &worst_reward)) {
                fprintf(stderr, "candidate failed packet=%u timer=%u\n", packet_grid.values[p], timer_grid.values[q]);
                if (csv != NULL) {
                    fclose(csv);
                }
                return 1;
            }
            for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
                csv_candidate(csv,
                              "candidate",
                              irq_traffic_name((irq_traffic_profile_t)t),
                              packet_grid.values[p],
                              timer_grid.values[q],
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
                best_mean.packet_threshold = packet_grid.values[p];
                best_mean.timer_threshold = timer_grid.values[q];
                best_mean.reward = mean_reward;
            }
            if (worst_reward > best_worst_reward) {
                best_worst_reward = worst_reward;
                best_worst_mean = mean_reward;
                best_worst.packet_threshold = packet_grid.values[p];
                best_worst.timer_threshold = timer_grid.values[q];
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
        eval_result_t eval;
        if (!irq_tune_eval_profile(cfg,
                                   (irq_traffic_profile_t)t,
                                   best_profile[t].packet_threshold,
                                   best_profile[t].timer_threshold,
                                   10001u,
                                   eval_seeds,
                                   &eval)) {
            fprintf(stderr, "eval failed for %s\n", irq_traffic_name((irq_traffic_profile_t)t));
            return 1;
        }
        csv_candidate(csv,
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
    if (!irq_tune_eval_all_profiles(cfg,
                                    best_mean.packet_threshold,
                                    best_mean.timer_threshold,
                                    10001u,
                                    eval_seeds,
                                    per_profile,
                                    &eval_mean,
                                    &eval_worst)) {
        fprintf(stderr, "global mean eval failed\n");
        return 1;
    }
    csv_candidate(csv,
                  "best_global_mean",
                  "all",
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

    if (!irq_tune_eval_all_profiles(cfg,
                                    best_worst.packet_threshold,
                                    best_worst.timer_threshold,
                                    10001u,
                                    eval_seeds,
                                    per_profile,
                                    &eval_mean,
                                    &eval_worst)) {
        fprintf(stderr, "global worst eval failed\n");
        return 1;
    }
    csv_candidate(csv,
                  "best_global_worst",
                  "all",
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
    printf("train global worst: packet=%u timer=%u mean=%.6f worst=%.6f\n",
           best_worst.packet_threshold,
           best_worst.timer_threshold,
           best_worst_mean,
           best_worst_reward);

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return 0;
}
