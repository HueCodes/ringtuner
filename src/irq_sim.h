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
    IRQ_TRAFFIC_ZERO_IDLE = 0,
    IRQ_TRAFFIC_STEADY_LOW,
    IRQ_TRAFFIC_STEADY_HIGH,
    IRQ_TRAFFIC_BURSTY,
    IRQ_TRAFFIC_ELEPHANT_MOUSE,
    IRQ_TRAFFIC_OVERLOAD_SPIKE,
    IRQ_TRAFFIC_COUNT
} irq_traffic_profile_t;

typedef enum {
    IRQ_POLICY_NO_COALESCING_ORACLE = 0,
    IRQ_POLICY_NO_COALESCING_CPU_LIMITED,
    IRQ_POLICY_FIXED_LOW_LATENCY,
    IRQ_POLICY_FIXED_BALANCED,
    IRQ_POLICY_FIXED_THROUGHPUT,
    IRQ_POLICY_SIMPLE_ADAPTIVE,
    IRQ_POLICY_ADAPTIVE_BANDIT,
    IRQ_POLICY_NAPI_POLLING,
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

typedef struct irq_sim irq_sim_t;

typedef bool (*irq_action_selector_fn)(const irq_sim_t *sim,
                                       const double obs[IRQ_SIM_OBS_SIZE],
                                       void *ctx,
                                       irq_action_t *action);

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
    uint32_t packet_threshold;
    uint32_t timer_threshold;
    double reward;
    double p99_latency;
    double drops;
    double interrupts;
    double delivered_ratio;
    double drop_ratio;
} irq_tuning_result_t;

typedef struct {
    const char *name;
    irq_sim_config_t cfg;
    uint32_t min_packet_threshold;
    uint32_t max_packet_threshold;
    uint32_t min_timer_threshold;
    uint32_t max_timer_threshold;
} irq_scenario_t;

typedef struct {
    double delivered_score;
    double latency_penalty;
    double tail_latency_penalty;
    double drop_penalty;
    double interrupt_cost_penalty;
    double setting_change_penalty;
    double unresolved_queue_penalty;
    double total;
} irq_reward_components_t;

typedef struct {
    uint64_t offered;
    uint64_t delivered;
    uint64_t dropped;
    uint64_t interrupts;
    uint64_t setting_changes;
    uint64_t latency_samples;
    uint32_t final_queue_depth;
    uint32_t max_queue_depth;
    double interrupt_cost;
    double delivered_ratio;
    double drop_ratio;
    double interrupts_per_delivered;
    double avg_batch_size;
    double avg_queue_depth;
    double p50_queue_depth;
    double p95_queue_depth;
    double p99_queue_depth;
    double avg_latency;
    double p50_latency;
    double p95_latency;
    double p99_latency;
    irq_reward_components_t reward_components;
    double reward;
} irq_sim_metrics_t;

struct irq_sim {
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
    uint64_t queue_depth_hist[IRQ_SIM_MAX_RING + 1u];
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
    uint32_t max_queue_depth;
    bool interrupt_per_packet;
    bool done;
};

irq_sim_config_t irq_sim_default_config(void);
const char *irq_traffic_name(irq_traffic_profile_t profile);
const char *irq_policy_name(irq_baseline_policy_t policy);
const irq_scenario_t *irq_scenario_by_index(size_t index);
const irq_scenario_t *irq_scenario_by_name(const char *name);
size_t irq_scenario_count(void);
bool irq_sim_validate_config(const irq_sim_config_t *cfg, char *err, size_t err_len);
bool irq_sim_reset(irq_sim_t *sim, const irq_sim_config_t *cfg);
bool irq_sim_step(irq_sim_t *sim);
bool irq_sim_step_arrivals(irq_sim_t *sim, uint32_t arrivals);
bool irq_sim_run_baseline(const irq_sim_config_t *cfg, irq_baseline_policy_t policy, irq_sim_metrics_t *out);
bool irq_sim_run_baseline_trace(const irq_sim_config_t *cfg,
                                irq_baseline_policy_t policy,
                                const uint32_t *arrivals,
                                uint64_t arrival_ticks,
                                irq_sim_metrics_t *out);
bool irq_sim_apply_baseline(irq_sim_t *sim, irq_baseline_policy_t policy);
bool irq_sim_apply_action(irq_sim_t *sim, irq_action_t action);
bool irq_sim_episode_step(irq_sim_t *sim, irq_action_t action, double obs[IRQ_SIM_OBS_SIZE], double *reward, bool *done);
bool irq_sim_run_control_loop(const irq_sim_config_t *cfg,
                              uint64_t control_interval,
                              irq_action_selector_fn select_action,
                              void *ctx,
                              irq_sim_metrics_t *out);
bool irq_sim_observation(const irq_sim_t *sim, double obs[IRQ_SIM_OBS_SIZE]);
double irq_sim_reward(const irq_sim_t *sim);
irq_reward_components_t irq_sim_reward_components(const irq_sim_metrics_t *metrics);
irq_sim_metrics_t irq_sim_metrics(const irq_sim_t *sim);
bool irq_tune_eval_profile(irq_sim_config_t base,
                           irq_traffic_profile_t traffic,
                           uint32_t packet_threshold,
                           uint32_t timer_threshold,
                           uint64_t seed_start,
                           uint64_t seed_count,
                           irq_tuning_result_t *out);
bool irq_tune_eval_profiles(irq_sim_config_t cfg,
                            const bool enabled[IRQ_TRAFFIC_COUNT],
                            uint32_t packet_threshold,
                            uint32_t timer_threshold,
                            uint64_t seed_start,
                            uint64_t seed_count,
                            irq_tuning_result_t per_profile[IRQ_TRAFFIC_COUNT],
                            double *mean_reward,
                            double *worst_reward);
bool irq_tune_eval_all_profiles(irq_sim_config_t cfg,
                                uint32_t packet_threshold,
                                uint32_t timer_threshold,
                                uint64_t seed_start,
                                uint64_t seed_count,
                                irq_tuning_result_t per_profile[IRQ_TRAFFIC_COUNT],
                                double *mean_reward,
                                double *worst_reward);
void irq_sim_clear_recent(irq_sim_t *sim);

#endif
