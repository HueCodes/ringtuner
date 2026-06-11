#include "irq_sim.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    uint32_t packet_threshold;
    uint32_t timer_threshold;
    double avg_reward;
    double avg_p99;
    double avg_interrupts;
    double avg_drops;
    uint64_t delivered;
    uint64_t dropped;
    uint64_t interrupts;
} tuning_result_t;

static const uint32_t packet_grid[] = {1u, 2u, 4u, 8u, 12u, 16u, 32u, 64u};
static const uint32_t timer_grid[] = {0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u};

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [--ticks N] [--seeds N] [--csv results/file.csv]\n", prog);
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

static bool csv_path_allowed(const char *path) {
    return path != NULL && strncmp(path, "results/", 8u) == 0 && strstr(path, "..") == NULL;
}

static bool run_fixed(const irq_sim_config_t *cfg, irq_sim_metrics_t *out) {
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

static bool eval_candidate(irq_sim_config_t base,
                           irq_traffic_profile_t traffic,
                           uint32_t packet_threshold,
                           uint32_t timer_threshold,
                           uint64_t seed_count,
                           tuning_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->packet_threshold = packet_threshold;
    out->timer_threshold = timer_threshold;
    base.traffic_profile = traffic;
    base.packet_threshold = packet_threshold;
    base.timer_threshold = timer_threshold;

    double reward_sum = 0.0;
    double p99_sum = 0.0;
    double interrupts_sum = 0.0;
    double drops_sum = 0.0;
    for (uint64_t i = 0u; i < seed_count; i++) {
        irq_sim_metrics_t m;
        base.seed = 1u + i;
        if (!run_fixed(&base, &m)) {
            return false;
        }
        reward_sum += m.reward;
        p99_sum += m.p99_latency;
        interrupts_sum += (double)m.interrupts;
        drops_sum += (double)m.dropped;
        out->delivered += m.delivered;
        out->dropped += m.dropped;
        out->interrupts += m.interrupts;
    }
    out->avg_reward = reward_sum / (double)seed_count;
    out->avg_p99 = p99_sum / (double)seed_count;
    out->avg_interrupts = interrupts_sum / (double)seed_count;
    out->avg_drops = drops_sum / (double)seed_count;
    return true;
}

static void maybe_write_csv(FILE *csv, irq_traffic_profile_t traffic, const tuning_result_t *r) {
    if (csv == NULL) {
        return;
    }
    fprintf(csv,
            "%s,%u,%u,%.9f,%.3f,%.3f,%.3f,%llu,%llu,%llu\n",
            irq_traffic_name(traffic),
            r->packet_threshold,
            r->timer_threshold,
            r->avg_reward,
            r->avg_p99,
            r->avg_interrupts,
            r->avg_drops,
            (unsigned long long)r->delivered,
            (unsigned long long)r->dropped,
            (unsigned long long)r->interrupts);
}

int main(int argc, char **argv) {
    irq_sim_config_t cfg = irq_sim_default_config();
    uint64_t seed_count = 16u;
    const char *csv_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &cfg.episode_ticks)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &seed_count) || seed_count == 0u || seed_count > 256u) {
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
        fprintf(csv, "traffic,packet_threshold,timer_threshold,avg_reward,avg_p99,avg_interrupts,avg_drops,delivered,dropped,interrupts\n");
    }

    printf("offline threshold tuning, ticks=%llu, seeds=%llu\n", (unsigned long long)cfg.episode_ticks, (unsigned long long)seed_count);
    printf("%-16s %8s %8s %11s %9s %12s %10s\n", "traffic", "pkt", "timer", "avg_reward", "avg_p99", "avg_intr", "avg_drops");

    tuning_result_t overall_best;
    memset(&overall_best, 0, sizeof(overall_best));
    overall_best.avg_reward = -DBL_MAX;
    irq_traffic_profile_t overall_traffic = IRQ_TRAFFIC_STEADY_LOW;

    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        tuning_result_t best;
        memset(&best, 0, sizeof(best));
        best.avg_reward = -DBL_MAX;
        for (size_t p = 0u; p < sizeof(packet_grid) / sizeof(packet_grid[0]); p++) {
            for (size_t q = 0u; q < sizeof(timer_grid) / sizeof(timer_grid[0]); q++) {
                tuning_result_t r;
                if (!eval_candidate(cfg, (irq_traffic_profile_t)t, packet_grid[p], timer_grid[q], seed_count, &r)) {
                    fprintf(stderr, "candidate failed for %s packet=%u timer=%u\n", irq_traffic_name((irq_traffic_profile_t)t), packet_grid[p], timer_grid[q]);
                    if (csv != NULL) {
                        fclose(csv);
                    }
                    return 1;
                }
                maybe_write_csv(csv, (irq_traffic_profile_t)t, &r);
                if (r.avg_reward > best.avg_reward) {
                    best = r;
                }
                if (r.avg_reward > overall_best.avg_reward) {
                    overall_best = r;
                    overall_traffic = (irq_traffic_profile_t)t;
                }
            }
        }
        printf("%-16s %8u %8u %11.6f %9.3f %12.3f %10.3f\n",
               irq_traffic_name((irq_traffic_profile_t)t),
               best.packet_threshold,
               best.timer_threshold,
               best.avg_reward,
               best.avg_p99,
               best.avg_interrupts,
               best.avg_drops);
    }

    printf("best single profile: %s packet=%u timer=%u avg_reward=%.6f\n",
           irq_traffic_name(overall_traffic),
           overall_best.packet_threshold,
           overall_best.timer_threshold,
           overall_best.avg_reward);

    if (csv != NULL && fclose(csv) != 0) {
        perror("fclose csv");
        return 1;
    }
    return 0;
}
