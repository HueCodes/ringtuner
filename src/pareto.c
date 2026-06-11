#include "irq_sim.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_GRID 64u
#define MAX_CANDIDATES (MAX_GRID * MAX_GRID)

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
    uint32_t packet_threshold;
    uint32_t timer_threshold;
    irq_tuning_result_t metric[IRQ_TRAFFIC_COUNT];
    double mean_reward;
    double worst_reward;
    bool dominated;
} candidate_t;

static void usage(const char *prog) {
    printf("usage: %s [--scenario NAME|all] [--traffic all|scenario|NAME|INDEX]\n"
           "          [--seeds N] [--packet-grid csv] [--timer-grid csv]\n"
           "          [--csv results/file.csv] [--help]\n",
           prog);
}

static void list_scenarios(FILE *out) {
    for (size_t i = 0u; i < irq_scenario_count(); i++) {
        fprintf(out, "%s\n", irq_scenario_by_index(i)->name);
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
    if (!custom_timer_grid &&
        !filter_grid_range(timer_grid, scenario->min_timer_threshold, scenario->max_timer_threshold)) {
        fprintf(stderr, "scenario %s timer threshold range has no candidates\n", scenario->name);
        return false;
    }
    return true;
}

static bool select_traffic(traffic_select_mode_t mode,
                           irq_traffic_profile_t traffic,
                           const irq_scenario_t *scenario,
                           bool enabled[IRQ_TRAFFIC_COUNT]) {
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        enabled[t] = false;
    }
    switch (mode) {
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
        enabled[traffic] = true;
        return true;
    }
    return false;
}

static bool dominates_one(const irq_tuning_result_t *a, const irq_tuning_result_t *b, bool *strict) {
    if (a->reward < b->reward || a->p99_latency > b->p99_latency || a->interrupts > b->interrupts ||
        a->drop_ratio > b->drop_ratio || a->delivered_ratio < b->delivered_ratio) {
        return false;
    }
    if (a->reward > b->reward || a->p99_latency < b->p99_latency || a->interrupts < b->interrupts ||
        a->drop_ratio < b->drop_ratio || a->delivered_ratio > b->delivered_ratio) {
        *strict = true;
    }
    return true;
}

static bool dominates(const candidate_t *a, const candidate_t *b, const bool enabled[IRQ_TRAFFIC_COUNT]) {
    bool strict = false;
    if (a->mean_reward < b->mean_reward || a->worst_reward < b->worst_reward) {
        return false;
    }
    if (a->mean_reward > b->mean_reward || a->worst_reward > b->worst_reward) {
        strict = true;
    }
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        if (enabled[t] && !dominates_one(&a->metric[t], &b->metric[t], &strict)) {
            return false;
        }
    }
    if (!strict && a->packet_threshold <= b->packet_threshold && a->timer_threshold <= b->timer_threshold &&
        (a->packet_threshold < b->packet_threshold || a->timer_threshold < b->timer_threshold)) {
        strict = true;
    }
    return strict;
}

static bool build_candidates(irq_sim_config_t cfg,
                             const grid_t *packet_grid,
                             const grid_t *timer_grid,
                             const bool enabled[IRQ_TRAFFIC_COUNT],
                             uint64_t seeds,
                             candidate_t candidates[MAX_CANDIDATES],
                             size_t *candidate_count) {
    *candidate_count = 0u;
    for (size_t p = 0u; p < packet_grid->count; p++) {
        for (size_t q = 0u; q < timer_grid->count; q++) {
            if (*candidate_count >= MAX_CANDIDATES) {
                return false;
            }
            candidate_t *c = &candidates[*candidate_count];
            memset(c, 0, sizeof(*c));
            c->packet_threshold = packet_grid->values[p];
            c->timer_threshold = timer_grid->values[q];
            if (!irq_tune_eval_profiles(cfg,
                                        enabled,
                                        c->packet_threshold,
                                        c->timer_threshold,
                                        1u,
                                        seeds,
                                        c->metric,
                                        &c->mean_reward,
                                        &c->worst_reward)) {
                return false;
            }
            (*candidate_count)++;
        }
    }

    for (size_t i = 0u; i < *candidate_count; i++) {
        for (size_t j = 0u; j < *candidate_count; j++) {
            if (i != j && dominates(&candidates[j], &candidates[i], enabled)) {
                candidates[i].dominated = true;
                break;
            }
        }
    }
    return true;
}

static void print_frontier(const char *scenario_name,
                           traffic_select_mode_t traffic_mode,
                           irq_traffic_profile_t selected_traffic,
                           const bool enabled[IRQ_TRAFFIC_COUNT],
                           const candidate_t candidates[MAX_CANDIDATES],
                           size_t candidate_count,
                           FILE *csv) {
    printf("\n%s traffic=%s pareto frontier\n",
           scenario_name,
           traffic_scope_name(traffic_mode, selected_traffic));
    printf("%-8s %-8s %12s %12s %9s %10s %9s %9s\n",
           "pkt",
           "timer",
           "mean_reward",
           "worst_reward",
           "p99",
           "interrupts",
           "drop",
           "traffic");

    for (size_t i = 0u; i < candidate_count; i++) {
        const candidate_t *c = &candidates[i];
        if (c->dominated) {
            continue;
        }
        for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
            if (!enabled[t]) {
                continue;
            }
            const irq_tuning_result_t *m = &c->metric[t];
            printf("%-8u %-8u %12.6f %12.6f %9.3f %10.3f %9.6f %9s\n",
                   c->packet_threshold,
                   c->timer_threshold,
                   c->mean_reward,
                   c->worst_reward,
                   m->p99_latency,
                   m->interrupts,
                   m->drop_ratio,
                   irq_traffic_name((irq_traffic_profile_t)t));
            if (csv != NULL) {
                fprintf(csv,
                        "%s,%s,%s,%u,%u,%.9f,%.9f,%.9f,%.3f,%.3f,%.6f,%.6f\n",
                        scenario_name,
                        traffic_scope_name(traffic_mode, selected_traffic),
                        irq_traffic_name((irq_traffic_profile_t)t),
                        c->packet_threshold,
                        c->timer_threshold,
                        c->mean_reward,
                        c->worst_reward,
                        m->reward,
                        m->p99_latency,
                        m->interrupts,
                        m->delivered_ratio,
                        m->drop_ratio);
            }
        }
    }
}

static bool run_scenario(const char *scenario_name,
                         irq_sim_config_t cfg,
                         const grid_t *packet_grid,
                         const grid_t *timer_grid,
                         traffic_select_mode_t traffic_mode,
                         irq_traffic_profile_t selected_traffic,
                         uint64_t seeds,
                         FILE *csv) {
    bool enabled[IRQ_TRAFFIC_COUNT];
    const irq_scenario_t *scenario = irq_scenario_by_name(scenario_name);
    candidate_t candidates[MAX_CANDIDATES];
    size_t candidate_count = 0u;
    if (!select_traffic(traffic_mode, selected_traffic, scenario, enabled) ||
        !build_candidates(cfg, packet_grid, timer_grid, enabled, seeds, candidates, &candidate_count)) {
        return false;
    }
    print_frontier(scenario_name, traffic_mode, selected_traffic, enabled, candidates, candidate_count, csv);
    return true;
}

int main(int argc, char **argv) {
    const char *scenario_name = "all";
    bool all_scenarios = true;
    traffic_select_mode_t traffic_mode = TRAFFIC_SELECT_SCENARIO;
    irq_traffic_profile_t selected_traffic = IRQ_TRAFFIC_STEADY_LOW;
    uint64_t seeds = 16u;
    const char *csv_path = NULL;
    grid_t base_packet_grid = {{1u, 2u, 4u, 8u, 12u, 16u, 32u, 64u}, 8u};
    grid_t base_timer_grid = {{0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}, 9u};
    bool custom_packet_grid = false;
    bool custom_timer_grid = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario_name = argv[++i];
            all_scenarios = strcmp(scenario_name, "all") == 0;
            if (!all_scenarios && irq_scenario_by_name(scenario_name) == NULL) {
                fprintf(stderr, "invalid scenario: %s\nvalid scenarios:\n", scenario_name);
                list_scenarios(stderr);
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
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &seeds) || seeds == 0u || seeds > 512u) {
                usage(argv[0]);
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
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (traffic_mode == TRAFFIC_SELECT_SCENARIO && !all_scenarios && irq_scenario_by_name(scenario_name) == NULL) {
        fprintf(stderr, "--traffic scenario requires a built-in scenario\n");
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
        fprintf(csv, "scenario,scope,traffic,packet_threshold,timer_threshold,mean_reward,worst_reward,reward,p99,interrupts,delivered_ratio,drop_ratio\n");
    }

    bool ok = true;
    if (all_scenarios) {
        for (size_t i = 0u; i < irq_scenario_count(); i++) {
            const irq_scenario_t *scenario = irq_scenario_by_index(i);
            grid_t packet_grid;
            grid_t timer_grid;
            if (!scenario_grids(scenario,
                                &base_packet_grid,
                                &base_timer_grid,
                                custom_packet_grid,
                                custom_timer_grid,
                                &packet_grid,
                                &timer_grid) ||
                !run_scenario(scenario->name,
                              scenario->cfg,
                              &packet_grid,
                              &timer_grid,
                              traffic_mode,
                              selected_traffic,
                              seeds,
                              csv)) {
                ok = false;
                break;
            }
        }
    } else {
        const irq_scenario_t *scenario = irq_scenario_by_name(scenario_name);
        grid_t packet_grid;
        grid_t timer_grid;
        if (scenario == NULL ||
            !scenario_grids(scenario,
                            &base_packet_grid,
                            &base_timer_grid,
                            custom_packet_grid,
                            custom_timer_grid,
                            &packet_grid,
                            &timer_grid) ||
            !run_scenario(scenario->name,
                          scenario->cfg,
                          &packet_grid,
                          &timer_grid,
                          traffic_mode,
                          selected_traffic,
                          seeds,
                          csv)) {
            ok = false;
        }
    }

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return ok ? 0 : 1;
}
