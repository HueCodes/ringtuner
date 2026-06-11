#include "irq_sim.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--ticks N] [--seed N] [--ring N] [--budget N]\n"
            "          [--profile NAME|INDEX] [--policy NAME|INDEX] [--direct]\n"
            "          [--packet-threshold N] [--timer-threshold N] [--csv results/file.csv]\n",
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

static bool csv_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
}

static void print_header(FILE *out) {
    fprintf(out,
            "%-16s %-18s %10s %8s %10s %10s %8s %8s %8s %9s %8s\n",
            "traffic",
            "policy",
            "delivered",
            "drops",
            "interrupts",
            "irq_cost",
            "p50",
            "p95",
            "p99",
            "reward",
            "ms");
}

static void print_row(FILE *out,
                      irq_traffic_profile_t traffic,
                      const char *policy,
                      const irq_sim_metrics_t *m,
                      double elapsed_ms) {
    fprintf(out,
            "%-16s %-18s %10llu %8llu %10llu %10.1f %8.1f %8.1f %8.1f %9.3f %8.2f\n",
            irq_traffic_name(traffic),
            policy,
            (unsigned long long)m->delivered,
            (unsigned long long)m->dropped,
            (unsigned long long)m->interrupts,
            m->interrupt_cost,
            m->p50_latency,
            m->p95_latency,
            m->p99_latency,
            m->reward,
            elapsed_ms);
}

static void csv_row(FILE *out,
                    irq_traffic_profile_t traffic,
                    const char *policy,
                    const irq_sim_metrics_t *m,
                    double elapsed_ms) {
    fprintf(out,
            "%s,%s,%llu,%llu,%llu,%llu,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.6f,%.3f\n",
            irq_traffic_name(traffic),
            policy,
            (unsigned long long)m->offered,
            (unsigned long long)m->delivered,
            (unsigned long long)m->dropped,
            (unsigned long long)m->interrupts,
            m->final_queue_depth,
            m->avg_queue_depth,
            m->interrupt_cost,
            m->p50_latency,
            m->p95_latency,
            m->p99_latency,
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

int main(int argc, char **argv) {
    irq_sim_config_t cfg = irq_sim_default_config();
    const char *csv_path = NULL;
    bool direct = false;
    bool one_profile = false;
    bool one_policy = false;
    irq_traffic_profile_t selected_profile = IRQ_TRAFFIC_STEADY_LOW;
    irq_baseline_policy_t selected_policy = IRQ_POLICY_FIXED_BALANCED;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
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
        } else if (strcmp(argv[i], "--direct") == 0) {
            direct = true;
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
        fprintf(csv, "traffic,policy,offered,delivered,drops,interrupts,final_queue_depth,avg_queue_depth,interrupt_cost,p50,p95,p99,reward,runtime_ms\n");
    }

    print_header(stdout);
    const int t_start = one_profile ? (int)selected_profile : 0;
    const int t_end = one_profile ? (int)selected_profile + 1 : (int)IRQ_TRAFFIC_COUNT;
    const int p_start = one_policy ? (int)selected_policy : 0;
    const int p_end = one_policy ? (int)selected_policy + 1 : (int)IRQ_POLICY_COUNT;

    for (int t = t_start; t < t_end; t++) {
        const int run_count = direct ? 1 : p_end - p_start;
        for (int j = 0; j < run_count; j++) {
            const int p = p_start + j;
            cfg.traffic_profile = (irq_traffic_profile_t)t;
            irq_sim_metrics_t m;
            const clock_t start = clock();
            const char *policy_name = direct ? "direct" : irq_policy_name((irq_baseline_policy_t)p);
            const bool ok = direct ? run_direct(&cfg, &m) : irq_sim_run_baseline(&cfg, (irq_baseline_policy_t)p, &m);
            if (!ok) {
                fprintf(stderr, "run failed for %s/%s\n", irq_traffic_name((irq_traffic_profile_t)t), policy_name);
                if (csv != NULL) {
                    fclose(csv);
                }
                return 1;
            }
            const clock_t end = clock();
            const double elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
            print_row(stdout, (irq_traffic_profile_t)t, policy_name, &m, elapsed_ms);
            if (csv != NULL) {
                csv_row(csv, (irq_traffic_profile_t)t, policy_name, &m, elapsed_ms);
            }
        }
    }

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return 0;
}
