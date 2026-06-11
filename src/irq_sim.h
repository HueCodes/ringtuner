#ifndef IRQ_SIM_H
#define IRQ_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IRQ_SIM_MAX_RING 4096u
#define IRQ_SIM_LATENCY_HIST 65536u
#define IRQ_SIM_MAX_TICKS 60000u
#define IRQ_SIM_OBS_SIZE 9u

typedef enum {
    IRQ_TRAFFIC_STEADY_LOW = 0,
    IRQ_TRAFFIC_STEADY_HIGH,
    IRQ_TRAFFIC_BURSTY,
    IRQ_TRAFFIC_ELEPHANT_MOUSE,
    IRQ_TRAFFIC_OVERLOAD_SPIKE,
    IRQ_TRAFFIC_COUNT
} irq_traffic_profile_t;

typedef enum {
    IRQ_POLICY_NO_COALESCING = 0,
    IRQ_POLICY_FIXED_LOW_LATENCY,
    IRQ_POLICY_FIXED_BALANCED,
    IRQ_POLICY_FIXED_THROUGHPUT,
    IRQ_POLICY_SIMPLE_ADAPTIVE,
    IRQ_POLICY_COUNT
} irq_baseline_policy_t;

typedef enum {
    IRQ_ACTION_LOW_LATENCY = 0,
    IRQ_ACTION_BALANCED_LOW,
    IRQ_ACTION_BALANCED_HIGH,
    IRQ_ACTION_THROUGHPUT,
    IRQ_ACTION_BULK,
    IRQ_ACTION_COUNT
} irq_action_t;

typedef struct {
    uint32_t ring_capacity;
    uint32_t service_budget;
    uint32_t packet_threshold;
    uint32_t timer_threshold;
    uint64_t episode_ticks;
    uint64_t seed;
    double interrupt_cost;
    irq_traffic_profile_t traffic_profile;
} irq_sim_config_t;

typedef struct {
    uint64_t offered;
    uint64_t delivered;
    uint64_t dropped;
    uint64_t interrupts;
    uint64_t setting_changes;
    uint32_t final_queue_depth;
    double interrupt_cost;
    double avg_queue_depth;
    double avg_latency;
    double p50_latency;
    double p95_latency;
    double p99_latency;
    double reward;
} irq_sim_metrics_t;

typedef struct {
    irq_sim_config_t cfg;
    uint64_t rng;
    uint64_t tick;
    uint32_t ring_head;
    uint32_t ring_tail;
    uint32_t ring_count;
    uint64_t first_queued_tick;
    bool have_first_queued_tick;
    uint64_t ring[IRQ_SIM_MAX_RING];
    uint64_t latency_hist[IRQ_SIM_LATENCY_HIST];
    uint64_t latency_samples;
    uint64_t latency_sum;
    uint64_t queue_depth_sum;
    uint64_t offered;
    uint64_t delivered;
    uint64_t dropped;
    uint64_t interrupts;
    uint64_t setting_changes;
    uint64_t recent_arrivals;
    uint64_t recent_delivered;
    uint64_t recent_dropped;
    uint64_t recent_interrupts;
    uint64_t recent_latency_sum;
    uint64_t recent_latency_samples;
    uint64_t recent_max_latency;
    uint32_t last_packet_threshold;
    uint32_t last_timer_threshold;
    bool interrupt_per_packet;
    bool done;
} irq_sim_t;

irq_sim_config_t irq_sim_default_config(void);
const char *irq_traffic_name(irq_traffic_profile_t profile);
const char *irq_policy_name(irq_baseline_policy_t policy);
bool irq_sim_validate_config(const irq_sim_config_t *cfg, char *err, size_t err_len);
bool irq_sim_reset(irq_sim_t *sim, const irq_sim_config_t *cfg);
bool irq_sim_step(irq_sim_t *sim);
bool irq_sim_run_baseline(const irq_sim_config_t *cfg, irq_baseline_policy_t policy, irq_sim_metrics_t *out);
bool irq_sim_apply_baseline(irq_sim_t *sim, irq_baseline_policy_t policy);
bool irq_sim_apply_action(irq_sim_t *sim, irq_action_t action);
bool irq_sim_episode_step(irq_sim_t *sim, irq_action_t action, double obs[IRQ_SIM_OBS_SIZE], double *reward, bool *done);
bool irq_sim_observation(const irq_sim_t *sim, double obs[IRQ_SIM_OBS_SIZE]);
double irq_sim_reward(const irq_sim_t *sim);
irq_sim_metrics_t irq_sim_metrics(const irq_sim_t *sim);
void irq_sim_clear_recent(irq_sim_t *sim);

#endif
