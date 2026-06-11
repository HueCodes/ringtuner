#include "irq_sim.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void usage(const char *prog) {
    printf(
            "usage: %s [--ticks N] [--seed N] [--ring N] [--budget N]\n"
            "          [--profile NAME|INDEX] [--policy NAME|INDEX] [--direct]\n"
            "          [--action NAME|INDEX] [--control-interval N]\n"
            "          [--packet-threshold N] [--timer-threshold N]\n"
            "          [--scenario NAME|all] [--csv results/file.csv]\n"
            "          [--list-profiles] [--list-policies] [--list-actions] [--list-scenarios] [--help]\n",
            prog);
}

static void list_profiles(FILE *out) {
    for (int i = 0; i < (int)IRQ_TRAFFIC_COUNT; i++) {
        fprintf(out, "%d %s\n", i, irq_traffic_name((irq_traffic_profile_t)i));
    }
}

static void list_policies(FILE *out) {
    for (int i = 0; i < (int)IRQ_POLICY_COUNT; i++) {
        fprintf(out, "%d %s\n", i, irq_policy_name((irq_baseline_policy_t)i));
    }
}

static const char *action_name(irq_action_t action) {
    static const char *names[] = {
        "low_latency",
        "balanced_low",
        "balanced_high",
        "throughput",
        "bulk",
    };
    if ((unsigned)action >= IRQ_ACTION_COUNT) {
        return "invalid";
    }
    return names[action];
}

static void list_actions(FILE *out) {
    for (int i = 0; i < (int)IRQ_ACTION_COUNT; i++) {
        fprintf(out, "%d %s\n", i, action_name((irq_action_t)i));
    }
}

static void list_scenarios(FILE *out) {
    for (size_t i = 0u; i < irq_scenario_count(); i++) {
        const irq_scenario_t *s = irq_scenario_by_index(i);
        fprintf(out,
                "%s profile=%s ring=%u budget=%u ticks=%llu cost=%.1f\n",
                s->name,
                irq_traffic_name(s->cfg.traffic_profile),
                s->cfg.ring_capacity,
                s->cfg.service_budget,
                (unsigned long long)s->cfg.episode_ticks,
                s->cfg.interrupt_cost);
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

static bool parse_u32_arg(const char *s, uint32_t *out) {
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

static bool parse_policy_arg(const char *s, irq_baseline_policy_t *out) {
    if (strcmp(s, "no_coalescing") == 0) {
        *out = IRQ_POLICY_NO_COALESCING_ORACLE;
        return true;
    }
    uint64_t idx = 0u;
    if (parse_u64(s, &idx) && idx < IRQ_POLICY_COUNT) {
        *out = (irq_baseline_policy_t)idx;
        return true;
    }
    for (int i = 0; i < (int)IRQ_POLICY_COUNT; i++) {
        if (strcmp(s, irq_policy_name((irq_baseline_policy_t)i)) == 0) {
            *out = (irq_baseline_policy_t)i;
            return true;
        }
    }
    return false;
}

static bool parse_action_arg(const char *s, irq_action_t *out) {
    uint64_t idx = 0u;
    if (parse_u64(s, &idx) && idx < IRQ_ACTION_COUNT) {
        *out = (irq_action_t)idx;
        return true;
    }
    for (int i = 0; i < (int)IRQ_ACTION_COUNT; i++) {
        if (strcmp(s, action_name((irq_action_t)i)) == 0) {
            *out = (irq_action_t)i;
            return true;
        }
    }
    return false;
}

static bool csv_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
}

static void print_header(FILE *out) {
    fprintf(out,
            "%-25s %-16s %-26s %9s %9s %7s %7s %7s %8s %8s %8s %8s %9s %8s\n",
            "scenario",
            "traffic",
            "policy",
            "offered",
            "delivered",
            "drops",
            "finalq",
            "interrupts",
            "irq/del",
            "p50",
            "p95",
            "p99",
            "reward",
            "ms");
}

static void print_row(FILE *out,
                      const char *scenario,
                      irq_traffic_profile_t traffic,
                      const char *policy,
                      const irq_sim_metrics_t *m,
                      double elapsed_ms) {
    fprintf(out,
            "%-25s %-16s %-26s %9llu %9llu %7llu %7u %7llu %8.3f %8.1f %8.1f %8.1f %9.3f %8.2f\n",
            scenario,
            irq_traffic_name(traffic),
            policy,
            (unsigned long long)m->offered,
            (unsigned long long)m->delivered,
            (unsigned long long)m->dropped,
            m->final_queue_depth,
            (unsigned long long)m->interrupts,
            m->interrupts_per_delivered,
            m->p50_latency,
            m->p95_latency,
            m->p99_latency,
            m->reward,
            elapsed_ms);
}

static void csv_row(FILE *out,
                    const char *scenario,
                    irq_traffic_profile_t traffic,
                    const char *policy,
                    const irq_sim_metrics_t *m,
                    double elapsed_ms) {
    fprintf(out,
            "%s,%s,%s,"
            "%llu,%llu,%llu,%.9f,%.9f,%llu,%u,%u,"
            "%.3f,%.6f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.3f,"
            "%llu,"
            "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,"
            "%.6f,%.3f\n",
            scenario,
            irq_traffic_name(traffic),
            policy,
            (unsigned long long)m->offered,
            (unsigned long long)m->delivered,
            (unsigned long long)m->dropped,
            m->delivered_ratio,
            m->drop_ratio,
            (unsigned long long)m->interrupts,
            m->final_queue_depth,
            m->max_queue_depth,
            m->avg_queue_depth,
            m->interrupts_per_delivered,
            m->avg_batch_size,
            m->interrupt_cost,
            m->p50_queue_depth,
            m->p95_queue_depth,
            m->p99_queue_depth,
            m->avg_latency,
            m->p50_latency,
            m->p95_latency,
            m->p99_latency,
            (unsigned long long)m->latency_samples,
            m->reward_components.delivered_score,
            m->reward_components.latency_penalty,
            m->reward_components.tail_latency_penalty,
            m->reward_components.drop_penalty,
            m->reward_components.interrupt_cost_penalty,
            m->reward_components.setting_change_penalty,
            m->reward_components.unresolved_queue_penalty,
            m->reward,
            elapsed_ms);
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

static bool fixed_action_selector(const irq_sim_t *sim, const double obs[IRQ_SIM_OBS_SIZE], void *ctx, irq_action_t *action) {
    (void)sim;
    (void)obs;
    *action = *(const irq_action_t *)ctx;
    return true;
}

static bool run_controlled_action(const irq_sim_config_t *cfg, irq_action_t action, uint64_t control_interval, irq_sim_metrics_t *out) {
    return irq_sim_run_control_loop(cfg, control_interval, fixed_action_selector, &action, out);
}

int main(int argc, char **argv) {
    irq_sim_config_t cfg = irq_sim_default_config();
    const char *csv_path = NULL;
    irq_sim_config_t cli_cfg = cfg;
    bool direct = false;
    bool one_profile = false;
    bool one_policy = false;
    bool one_action = false;
    const char *scenario_name = NULL;
    bool all_scenarios = false;
    uint64_t control_interval = 32u;
    irq_traffic_profile_t selected_profile = IRQ_TRAFFIC_STEADY_LOW;
    irq_baseline_policy_t selected_policy = IRQ_POLICY_FIXED_BALANCED;
    irq_action_t selected_action = IRQ_ACTION_BALANCED_LOW;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list-profiles") == 0) {
            list_profiles(stdout);
            return 0;
        } else if (strcmp(argv[i], "--list-policies") == 0) {
            list_policies(stdout);
            return 0;
        } else if (strcmp(argv[i], "--list-actions") == 0) {
            list_actions(stdout);
            return 0;
        } else if (strcmp(argv[i], "--list-scenarios") == 0) {
            list_scenarios(stdout);
            return 0;
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &cfg.episode_ticks)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &cfg.seed)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[++i], &cfg.ring_capacity)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--budget") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[++i], &cfg.service_budget)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--packet-threshold") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[++i], &cfg.packet_threshold)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--timer-threshold") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[++i], &cfg.timer_threshold)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            if (!parse_traffic_arg(argv[++i], &selected_profile)) {
                usage(argv[0]);
                return 2;
            }
            one_profile = true;
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            if (!parse_policy_arg(argv[++i], &selected_policy)) {
                usage(argv[0]);
                return 2;
            }
            one_policy = true;
        } else if (strcmp(argv[i], "--action") == 0 && i + 1 < argc) {
            if (!parse_action_arg(argv[++i], &selected_action)) {
                fprintf(stderr, "invalid action\nvalid actions:\n");
                list_actions(stderr);
                return 2;
            }
            one_action = true;
        } else if (strcmp(argv[i], "--control-interval") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &control_interval) || control_interval == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--direct") == 0) {
            direct = true;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario_name = argv[++i];
            all_scenarios = strcmp(scenario_name, "all") == 0;
            if (!all_scenarios && irq_scenario_by_name(scenario_name) == NULL) {
                fprintf(stderr, "invalid scenario: %s\nvalid scenarios:\n", scenario_name);
                list_scenarios(stderr);
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
            fprintf(stderr, "valid profiles:\n");
            list_profiles(stderr);
            fprintf(stderr, "valid policies:\n");
            list_policies(stderr);
            return 2;
        }
    }

    if (direct && one_action) {
        fprintf(stderr, "--direct and --action are mutually exclusive\n");
        return 2;
    }

    char err[128];
    if (!irq_sim_validate_config(&cfg, err, sizeof(err))) {
        fprintf(stderr, "invalid config: %s\n", err);
        return 2;
    }
    cli_cfg = cfg;

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
        fprintf(csv,
                "scenario,traffic,policy,offered,delivered,drops,delivered_ratio,drop_ratio,interrupts,final_queue_depth,max_queue_depth,avg_queue_depth,interrupts_per_delivered,avg_batch_size,interrupt_cost,p50_queue_depth,p95_queue_depth,p99_queue_depth,avg_latency,p50_latency,p95_latency,p99_latency,latency_samples,delivered_score,latency_penalty,tail_latency_penalty,drop_penalty,interrupt_cost_penalty,setting_change_penalty,unresolved_queue_penalty,reward,runtime_ms\n");
    }

    print_header(stdout);
    const size_t run_scenarios = all_scenarios ? irq_scenario_count() : 1u;

    for (size_t scenario_idx = 0u; scenario_idx < run_scenarios; scenario_idx++) {
        const char *run_scenario_name = "default";
        irq_sim_config_t run_cfg_base = cli_cfg;
        if (scenario_name != NULL) {
            const irq_scenario_t *scenario = all_scenarios ? irq_scenario_by_index(scenario_idx) : irq_scenario_by_name(scenario_name);
            if (scenario == NULL) {
                continue;
            }
            run_scenario_name = scenario->name;
            run_cfg_base = scenario->cfg;
            run_cfg_base.seed = cli_cfg.seed;
            if (one_profile) {
                run_cfg_base.traffic_profile = selected_profile;
            }
        }
        const int t_start = one_profile ? (int)selected_profile : (scenario_name != NULL ? (int)run_cfg_base.traffic_profile : 0);
        const int t_end = one_profile ? (int)selected_profile + 1 : (scenario_name != NULL ? (int)run_cfg_base.traffic_profile + 1 : (int)IRQ_TRAFFIC_COUNT);
        const int p_start = one_policy ? (int)selected_policy : 0;
        const int p_end = one_policy ? (int)selected_policy + 1 : (int)IRQ_POLICY_COUNT;

        for (int t = t_start; t < t_end; t++) {
            const int run_count = (direct || one_action) ? 1 : p_end - p_start;
            for (int j = 0; j < run_count; j++) {
                const int p = p_start + j;
                cfg = run_cfg_base;
                cfg.traffic_profile = (irq_traffic_profile_t)t;
                irq_sim_metrics_t m;
                const clock_t start = clock();
                char controlled_name[96];
                const char *policy_name = direct ? "direct" : irq_policy_name((irq_baseline_policy_t)p);
                bool ok = false;
                if (one_action) {
                    (void)snprintf(controlled_name,
                                   sizeof(controlled_name),
                                   "control_%s_%llut",
                                   action_name(selected_action),
                                   (unsigned long long)control_interval);
                    policy_name = controlled_name;
                    ok = run_controlled_action(&cfg, selected_action, control_interval, &m);
                } else {
                    ok = direct ? run_direct(&cfg, &m) : irq_sim_run_baseline(&cfg, (irq_baseline_policy_t)p, &m);
                }
                if (!ok) {
                    fprintf(stderr, "run failed for %s/%s\n", irq_traffic_name((irq_traffic_profile_t)t), policy_name);
                    if (csv != NULL) {
                        fclose(csv);
                    }
                    return 1;
                }
                const clock_t end = clock();
                const double elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
                print_row(stdout, run_scenario_name, (irq_traffic_profile_t)t, policy_name, &m, elapsed_ms);
                if (csv != NULL) {
                    csv_row(csv, run_scenario_name, (irq_traffic_profile_t)t, policy_name, &m, elapsed_ms);
                }
            }
        }
        if (scenario_name == NULL) {
            break;
        }
    }

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return 0;
}
