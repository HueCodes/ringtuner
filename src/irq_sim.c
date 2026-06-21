#include "irq_sim.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const irq_scenario_t scenarios[] = {
    {
        "latency_sensitive_low_load",
        {128u, 32u, 2u, 4u, 4000u, 1u, 1.5, IRQ_TRAFFIC_STEADY_LOW},
        1u,
        16u,
        0u,
        32u,
    },
    {
        "high_throughput_steady_load",
        {512u, 64u, 32u, 64u, 6000u, 1u, 1.0, IRQ_TRAFFIC_STEADY_HIGH},
        4u,
        128u,
        4u,
        160u,
    },
    {
        "microburst_workload",
        {256u, 32u, 8u, 16u, 5000u, 1u, 1.0, IRQ_TRAFFIC_BURSTY},
        1u,
        64u,
        0u,
        96u,
    },
    {
        "elephant_flow_burst",
        {384u, 48u, 16u, 32u, 5000u, 1u, 1.0, IRQ_TRAFFIC_ELEPHANT_MOUSE},
        1u,
        96u,
        0u,
        128u,
    },
    {
        "overload_with_recovery",
        {256u, 32u, 8u, 16u, 5000u, 1u, 1.0, IRQ_TRAFFIC_OVERLOAD_SPIKE},
        1u,
        64u,
        0u,
        128u,
    },
    {
        "small_rx_ring_stress",
        {16u, 8u, 8u, 16u, 4000u, 1u, 1.0, IRQ_TRAFFIC_BURSTY},
        1u,
        16u,
        0u,
        64u,
    },
    {
        "low_cpu_budget_stress",
        {128u, 4u, 4u, 8u, 4000u, 1u, 1.0, IRQ_TRAFFIC_STEADY_HIGH},
        1u,
        64u,
        0u,
        128u,
    },
};

static uint64_t sat_add_u64(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

static double clamp01(double v) {
    if (!isfinite(v) || v <= 0.0) {
        return 0.0;
    }
    if (v >= 1.0) {
        return 1.0;
    }
    return v;
}

static uint64_t rng_next(irq_sim_t *sim) {
    uint64_t x = sim->rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    sim->rng = x == 0u ? 0x9e3779b97f4a7c15ull : x;
    return sim->rng;
}

static bool chance_per_1024(irq_sim_t *sim, uint32_t n) {
    return (rng_next(sim) & 1023u) < n;
}

static uint32_t traffic_arrivals(irq_sim_t *sim) {
    const uint64_t t = sim->tick;
    switch (sim->cfg.traffic_profile) {
    case IRQ_TRAFFIC_ZERO_IDLE:
        return 0u;
    case IRQ_TRAFFIC_STEADY_LOW:
        return chance_per_1024(sim, 160u) ? 1u : 0u;
    case IRQ_TRAFFIC_STEADY_HIGH: {
        uint32_t n = chance_per_1024(sim, 850u) ? 1u : 0u;
        n += chance_per_1024(sim, 260u) ? 1u : 0u;
        return n;
    }
    case IRQ_TRAFFIC_BURSTY: {
        uint32_t n = chance_per_1024(sim, 70u) ? 1u : 0u;
        if ((t % 160u) >= 40u && (t % 160u) < 56u) {
            n += 4u + (uint32_t)(rng_next(sim) % 3u);
        }
        return n;
    }
    case IRQ_TRAFFIC_ELEPHANT_MOUSE: {
        uint32_t n = chance_per_1024(sim, 260u) ? 1u : 0u;
        if ((t % 240u) >= 80u && (t % 240u) < 140u) {
            n += 2u + (uint32_t)(rng_next(sim) % 4u);
        }
        return n;
    }
    case IRQ_TRAFFIC_OVERLOAD_SPIKE: {
        uint32_t n = chance_per_1024(sim, 120u) ? 1u : 0u;
        const uint64_t start = sim->cfg.episode_ticks / 3u;
        const uint64_t end = start + sim->cfg.episode_ticks / 5u;
        if (t >= start && t < end) {
            n += 36u + (uint32_t)(rng_next(sim) % 13u);
        }
        return n;
    }
    case IRQ_TRAFFIC_COUNT:
        return 0u;
    }
    return 0u;
}

irq_sim_config_t irq_sim_default_config(void) {
    irq_sim_config_t cfg;
    cfg.ring_capacity = 256u;
    cfg.service_budget = 32u;
    cfg.packet_threshold = 8u;
    cfg.timer_threshold = 16u;
    cfg.episode_ticks = 5000u;
    cfg.seed = 1u;
    cfg.interrupt_cost = 1.0;
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_LOW;
    return cfg;
}

const char *irq_traffic_name(irq_traffic_profile_t profile) {
    static const char *names[] = {
        "zero_idle",
        "steady_low",
        "steady_high",
        "bursty",
        "elephant_mouse",
        "overload_spike",
    };
    if ((unsigned)profile >= IRQ_TRAFFIC_COUNT) {
        return "invalid";
    }
    return names[profile];
}

const char *irq_policy_name(irq_baseline_policy_t policy) {
    static const char *names[] = {
        "no_coalescing_oracle",
        "no_coalescing_cpu_limited",
        "fixed_low_latency",
        "fixed_balanced",
        "fixed_throughput",
        "simple_adaptive",
        "adaptive_bandit",
        "napi_polling",
    };
    if ((unsigned)policy >= IRQ_POLICY_COUNT) {
        return "invalid";
    }
    return names[policy];
}

size_t irq_scenario_count(void) {
    return sizeof(scenarios) / sizeof(scenarios[0]);
}

const irq_scenario_t *irq_scenario_by_index(size_t index) {
    if (index >= irq_scenario_count()) {
        return NULL;
    }
    return &scenarios[index];
}

const irq_scenario_t *irq_scenario_by_name(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < irq_scenario_count(); i++) {
        if (strcmp(name, scenarios[i].name) == 0) {
            return &scenarios[i];
        }
    }
    return NULL;
}

bool irq_sim_validate_config(const irq_sim_config_t *cfg, char *err, size_t err_len) {
    const char *msg = NULL;
    if (cfg == NULL) {
        msg = "config is null";
    } else if (cfg->ring_capacity == 0u || cfg->ring_capacity > IRQ_SIM_MAX_RING) {
        msg = "ring_capacity out of range";
    } else if (cfg->service_budget == 0u || cfg->service_budget > cfg->ring_capacity) {
        msg = "service_budget out of range";
    } else if (cfg->packet_threshold == 0u || cfg->packet_threshold > cfg->ring_capacity) {
        msg = "packet_threshold out of range";
    } else if (cfg->timer_threshold >= IRQ_SIM_LATENCY_HIST) {
        msg = "timer_threshold out of range";
    } else if (cfg->episode_ticks == 0u || cfg->episode_ticks > IRQ_SIM_MAX_TICKS) {
        msg = "episode_ticks out of range";
    } else if (!isfinite(cfg->interrupt_cost) || cfg->interrupt_cost < 0.0 || cfg->interrupt_cost > 1000000.0) {
        msg = "interrupt_cost out of range";
    } else if ((unsigned)cfg->traffic_profile >= IRQ_TRAFFIC_COUNT) {
        msg = "traffic_profile out of range";
    }
    if (msg != NULL && err != NULL && err_len > 0u) {
        (void)snprintf(err, err_len, "%s", msg);
    }
    return msg == NULL;
}

static void set_thresholds(irq_sim_t *sim, uint32_t packet_threshold, uint32_t timer_threshold) {
    if (packet_threshold == 0u) {
        packet_threshold = 1u;
    }
    if (packet_threshold > sim->cfg.ring_capacity) {
        packet_threshold = sim->cfg.ring_capacity;
    }
    if (timer_threshold >= IRQ_SIM_LATENCY_HIST) {
        timer_threshold = IRQ_SIM_LATENCY_HIST - 1u;
    }
    if (sim->tick > 0u && (packet_threshold != sim->cfg.packet_threshold || timer_threshold != sim->cfg.timer_threshold)) {
        sim->setting_changes = sat_add_u64(sim->setting_changes, 1u);
    }
    sim->cfg.packet_threshold = packet_threshold;
    sim->cfg.timer_threshold = timer_threshold;
}

bool irq_sim_reset(irq_sim_t *sim, const irq_sim_config_t *cfg) {
    char err[96];
    if (sim == NULL || !irq_sim_validate_config(cfg, err, sizeof(err))) {
        return false;
    }
    memset(sim, 0, sizeof(*sim));
    sim->cfg = *cfg;
    sim->rng = cfg->seed == 0u ? 0x9e3779b97f4a7c15ull : cfg->seed;
    sim->last_packet_threshold = cfg->packet_threshold;
    sim->last_timer_threshold = cfg->timer_threshold;
    return true;
}

static void enqueue_arrival(irq_sim_t *sim) {
    sim->offered = sat_add_u64(sim->offered, 1u);
    sim->recent_arrivals = sat_add_u64(sim->recent_arrivals, 1u);
    if (sim->ring_count == sim->cfg.ring_capacity) {
        sim->dropped = sat_add_u64(sim->dropped, 1u);
        sim->recent_dropped = sat_add_u64(sim->recent_dropped, 1u);
        return;
    }
    sim->ring[sim->ring_tail] = sim->tick;
    sim->ring_tail = (sim->ring_tail + 1u) % sim->cfg.ring_capacity;
    sim->ring_count++;
    if (!sim->have_first_queued_tick) {
        sim->first_queued_tick = sim->tick;
        sim->have_first_queued_tick = true;
    }
    if (sim->ring_count > sim->max_queue_depth) {
        sim->max_queue_depth = sim->ring_count;
    }
}

static void record_latency(irq_sim_t *sim, uint64_t latency) {
    const uint64_t bucket = latency >= IRQ_SIM_LATENCY_HIST ? IRQ_SIM_LATENCY_HIST - 1u : latency;
    sim->latency_hist[bucket] = sat_add_u64(sim->latency_hist[bucket], 1u);
    sim->latency_samples = sat_add_u64(sim->latency_samples, 1u);
    sim->latency_sum = sat_add_u64(sim->latency_sum, latency);
    sim->recent_latency_sum = sat_add_u64(sim->recent_latency_sum, latency);
    sim->recent_latency_samples = sat_add_u64(sim->recent_latency_samples, 1u);
    if (latency > sim->recent_max_latency) {
        sim->recent_max_latency = latency;
    }
}

static bool interrupt_due(const irq_sim_t *sim) {
    if (sim->ring_count == 0u) {
        return false;
    }
    if (sim->ring_count >= sim->cfg.packet_threshold) {
        return true;
    }
    if (!sim->have_first_queued_tick) {
        return false;
    }
    return (sim->tick - sim->first_queued_tick) >= sim->cfg.timer_threshold;
}

static void service_batch(irq_sim_t *sim, bool count_interrupt) {
    uint32_t n = sim->interrupt_per_packet ? 1u : sim->cfg.service_budget;
    if (n > sim->ring_count) {
        n = sim->ring_count;
    }
    if (count_interrupt) {
        sim->interrupts = sat_add_u64(sim->interrupts, 1u);
        sim->recent_interrupts = sat_add_u64(sim->recent_interrupts, 1u);
    }
    for (uint32_t i = 0u; i < n; i++) {
        const uint64_t arrival_tick = sim->ring[sim->ring_head];
        const uint64_t latency = sim->tick - arrival_tick + 1u;
        sim->ring_head = (sim->ring_head + 1u) % sim->cfg.ring_capacity;
        sim->ring_count--;
        sim->delivered = sat_add_u64(sim->delivered, 1u);
        sim->recent_delivered = sat_add_u64(sim->recent_delivered, 1u);
        record_latency(sim, latency);
    }
    if (sim->ring_count == 0u) {
        sim->have_first_queued_tick = false;
    } else {
        sim->first_queued_tick = sim->ring[sim->ring_head];
        sim->have_first_queued_tick = true;
    }
}

static void service_interrupt(irq_sim_t *sim) {
    service_batch(sim, true);
}

static void record_queue_depth(irq_sim_t *sim) {
    uint32_t depth = sim->ring_count;
    if (depth > IRQ_SIM_MAX_RING) {
        depth = IRQ_SIM_MAX_RING;
    }
    sim->queue_depth_sum = sat_add_u64(sim->queue_depth_sum, sim->ring_count);
    sim->queue_depth_hist[depth] = sat_add_u64(sim->queue_depth_hist[depth], 1u);
    if (sim->ring_count > sim->max_queue_depth) {
        sim->max_queue_depth = sim->ring_count;
    }
}

bool irq_sim_step_arrivals(irq_sim_t *sim, uint32_t arrivals) {
    if (sim == NULL || sim->done) {
        return false;
    }
    for (uint32_t i = 0u; i < arrivals; i++) {
        enqueue_arrival(sim);
    }
    if (interrupt_due(sim)) {
        if (sim->interrupt_per_packet) {
            while (sim->ring_count > 0u) {
                service_interrupt(sim);
            }
        } else {
            service_interrupt(sim);
        }
    }
    record_queue_depth(sim);
    sim->tick++;
    if (sim->tick >= sim->cfg.episode_ticks) {
        sim->done = true;
    }
    return true;
}

bool irq_sim_step(irq_sim_t *sim) {
    if (sim == NULL || sim->done) {
        return false;
    }
    return irq_sim_step_arrivals(sim, traffic_arrivals(sim));
}

bool irq_sim_apply_baseline(irq_sim_t *sim, irq_baseline_policy_t policy) {
    if (sim == NULL || (unsigned)policy >= IRQ_POLICY_COUNT) {
        return false;
    }
    switch (policy) {
    case IRQ_POLICY_NO_COALESCING_ORACLE:
        sim->interrupt_per_packet = true;
        set_thresholds(sim, 1u, 0u);
        break;
    case IRQ_POLICY_NO_COALESCING_CPU_LIMITED:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 1u, 0u);
        break;
    case IRQ_POLICY_FIXED_LOW_LATENCY:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 2u, 4u);
        break;
    case IRQ_POLICY_FIXED_BALANCED:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 8u, 16u);
        break;
    case IRQ_POLICY_FIXED_THROUGHPUT:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 32u, 64u);
        break;
    case IRQ_POLICY_SIMPLE_ADAPTIVE: {
        sim->interrupt_per_packet = false;
        const bool drops = sim->recent_dropped > 0u;
        const double recent_avg = sim->recent_latency_samples == 0u ? 0.0 : (double)sim->recent_latency_sum / (double)sim->recent_latency_samples;
        if (drops || recent_avg > 80.0) {
            set_thresholds(sim, 2u, 4u);
        } else if (sim->recent_arrivals > sim->cfg.service_budget) {
            set_thresholds(sim, 32u, 48u);
        } else if (sim->recent_arrivals > sim->cfg.service_budget / 2u) {
            set_thresholds(sim, 12u, 20u);
        } else {
            set_thresholds(sim, 4u, 8u);
        }
        irq_sim_clear_recent(sim);
        break;
    }
    case IRQ_POLICY_ADAPTIVE_BANDIT:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 8u, 16u);
        break;
    case IRQ_POLICY_NAPI_POLLING:
        sim->interrupt_per_packet = false;
        set_thresholds(sim, 8u, 16u);
        break;
    case IRQ_POLICY_COUNT:
        return false;
    }
    return true;
}

bool irq_sim_apply_action(irq_sim_t *sim, irq_action_t action) {
    if (sim == NULL || (unsigned)action >= IRQ_ACTION_COUNT) {
        return false;
    }
    sim->interrupt_per_packet = false;
    switch (action) {
    case IRQ_ACTION_LOW_LATENCY:
        set_thresholds(sim, 1u, 1u);
        break;
    case IRQ_ACTION_BALANCED_LOW:
        set_thresholds(sim, 4u, 8u);
        break;
    case IRQ_ACTION_BALANCED_HIGH:
        set_thresholds(sim, 12u, 20u);
        break;
    case IRQ_ACTION_THROUGHPUT:
        set_thresholds(sim, 32u, 64u);
        break;
    case IRQ_ACTION_BULK:
        set_thresholds(sim, 64u, 128u);
        break;
    case IRQ_ACTION_COUNT:
        return false;
    }
    return true;
}

bool irq_sim_episode_step(irq_sim_t *sim, irq_action_t action, double obs[IRQ_SIM_OBS_SIZE], double *reward, bool *done) {
    if (sim == NULL || reward == NULL || done == NULL) {
        return false;
    }
    const irq_sim_metrics_t before = irq_sim_metrics(sim);
    irq_sim_clear_recent(sim);
    if (!irq_sim_apply_action(sim, action)) {
        return false;
    }
    if (!irq_sim_step(sim)) {
        return false;
    }
    const irq_sim_metrics_t after = irq_sim_metrics(sim);
    *reward = after.reward - before.reward;
    *done = sim->done;
    if (obs != NULL && !irq_sim_observation(sim, obs)) {
        return false;
    }
    return isfinite(*reward);
}

bool irq_sim_run_control_loop(const irq_sim_config_t *cfg,
                              uint64_t control_interval,
                              irq_action_selector_fn select_action,
                              void *ctx,
                              irq_sim_metrics_t *out) {
    if (out == NULL || select_action == NULL || control_interval == 0u) {
        return false;
    }
    irq_sim_t sim;
    if (!irq_sim_reset(&sim, cfg)) {
        return false;
    }

    while (!sim.done) {
        double obs[IRQ_SIM_OBS_SIZE];
        irq_action_t action = IRQ_ACTION_BALANCED_LOW;
        if (!irq_sim_observation(&sim, obs) || !select_action(&sim, obs, ctx, &action)) {
            return false;
        }
        if (!irq_sim_apply_action(&sim, action)) {
            return false;
        }
        irq_sim_clear_recent(&sim);
        for (uint64_t i = 0u; i < control_interval && !sim.done; i++) {
            if (!irq_sim_step(&sim)) {
                return false;
            }
        }
    }
    *out = irq_sim_metrics(&sim);
    return true;
}

static double percentile(const irq_sim_t *sim, double q) {
    if (sim->latency_samples == 0u) {
        return 0.0;
    }
    uint64_t rank = (uint64_t)ceil(q * (double)sim->latency_samples);
    if (rank == 0u) {
        rank = 1u;
    }
    uint64_t seen = 0u;
    for (uint32_t i = 0u; i < IRQ_SIM_LATENCY_HIST; i++) {
        seen = sat_add_u64(seen, sim->latency_hist[i]);
        if (seen >= rank) {
            return (double)i;
        }
    }
    return (double)(IRQ_SIM_LATENCY_HIST - 1u);
}

static double queue_percentile(const irq_sim_t *sim, double q) {
    if (sim->tick == 0u) {
        return 0.0;
    }
    uint64_t rank = (uint64_t)ceil(q * (double)sim->tick);
    if (rank == 0u) {
        rank = 1u;
    }
    uint64_t seen = 0u;
    for (uint32_t i = 0u; i <= IRQ_SIM_MAX_RING; i++) {
        seen = sat_add_u64(seen, sim->queue_depth_hist[i]);
        if (seen >= rank) {
            return (double)i;
        }
    }
    return (double)IRQ_SIM_MAX_RING;
}

irq_reward_components_t irq_sim_reward_components(const irq_sim_metrics_t *metrics) {
    irq_reward_components_t c;
    memset(&c, 0, sizeof(c));
    if (metrics == NULL) {
        return c;
    }
    const double offered = metrics->offered == 0u ? 1.0 : (double)metrics->offered;
    c.delivered_score = (double)metrics->delivered / offered;
    c.latency_penalty = metrics->avg_latency / 200.0;
    c.tail_latency_penalty = metrics->p99_latency / 400.0;
    c.drop_penalty = 5.0 * ((double)metrics->dropped / offered);
    c.interrupt_cost_penalty = metrics->interrupt_cost / (offered * 50.0);
    c.setting_change_penalty = (double)metrics->setting_changes * 0.001;
    c.unresolved_queue_penalty = 3.0 * ((double)metrics->final_queue_depth / offered);
    c.total = c.delivered_score - c.latency_penalty - c.tail_latency_penalty - c.drop_penalty -
              c.interrupt_cost_penalty - c.setting_change_penalty - c.unresolved_queue_penalty;
    if (!isfinite(c.total)) {
        c.total = -DBL_MAX;
    }
    return c;
}

irq_sim_metrics_t irq_sim_metrics(const irq_sim_t *sim) {
    irq_sim_metrics_t m;
    memset(&m, 0, sizeof(m));
    if (sim == NULL) {
        return m;
    }
    m.offered = sim->offered;
    m.delivered = sim->delivered;
    m.dropped = sim->dropped;
    m.interrupts = sim->interrupts;
    m.setting_changes = sim->setting_changes;
    m.latency_samples = sim->latency_samples;
    m.final_queue_depth = sim->ring_count;
    m.max_queue_depth = sim->max_queue_depth;
    m.interrupt_cost = (double)sim->interrupts * sim->cfg.interrupt_cost;
    const double offered = m.offered == 0u ? 1.0 : (double)m.offered;
    m.delivered_ratio = (double)m.delivered / offered;
    m.drop_ratio = (double)m.dropped / offered;
    m.interrupts_per_delivered = m.delivered == 0u ? 0.0 : (double)m.interrupts / (double)m.delivered;
    m.avg_batch_size = m.interrupts == 0u ? 0.0 : (double)m.delivered / (double)m.interrupts;
    m.avg_queue_depth = sim->tick == 0u ? 0.0 : (double)sim->queue_depth_sum / (double)sim->tick;
    m.p50_queue_depth = queue_percentile(sim, 0.50);
    m.p95_queue_depth = queue_percentile(sim, 0.95);
    m.p99_queue_depth = queue_percentile(sim, 0.99);
    m.avg_latency = sim->latency_samples == 0u ? 0.0 : (double)sim->latency_sum / (double)sim->latency_samples;
    m.p50_latency = percentile(sim, 0.50);
    m.p95_latency = percentile(sim, 0.95);
    m.p99_latency = percentile(sim, 0.99);
    m.reward_components = irq_sim_reward_components(&m);
    m.reward = m.reward_components.total;
    return m;
}

static bool run_fixed_config(const irq_sim_config_t *cfg, irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (out == NULL || !irq_sim_reset(&sim, cfg)) {
        return false;
    }
    while (!sim.done) {
        (void)irq_sim_step(&sim);
    }
    *out = irq_sim_metrics(&sim);
    return true;
}

bool irq_tune_eval_profile(irq_sim_config_t base,
                           irq_traffic_profile_t traffic,
                           uint32_t packet_threshold,
                           uint32_t timer_threshold,
                           uint64_t seed_start,
                           uint64_t seed_count,
                           irq_tuning_result_t *out) {
    if (out == NULL || seed_count == 0u) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->packet_threshold = packet_threshold;
    out->timer_threshold = timer_threshold;
    base.traffic_profile = traffic;
    base.packet_threshold = packet_threshold;
    base.timer_threshold = timer_threshold;
    char err[96];
    if (!irq_sim_validate_config(&base, err, sizeof(err))) {
        return false;
    }

    double reward_sum = 0.0;
    double p99_sum = 0.0;
    double drops_sum = 0.0;
    double interrupts_sum = 0.0;
    double delivered_ratio_sum = 0.0;
    double drop_ratio_sum = 0.0;
    for (uint64_t i = 0u; i < seed_count; i++) {
        irq_sim_metrics_t m;
        base.seed = seed_start + i;
        if (!run_fixed_config(&base, &m)) {
            return false;
        }
        reward_sum += m.reward;
        p99_sum += m.p99_latency;
        drops_sum += (double)m.dropped;
        interrupts_sum += (double)m.interrupts;
        delivered_ratio_sum += m.delivered_ratio;
        drop_ratio_sum += m.drop_ratio;
    }
    out->reward = reward_sum / (double)seed_count;
    out->p99_latency = p99_sum / (double)seed_count;
    out->drops = drops_sum / (double)seed_count;
    out->interrupts = interrupts_sum / (double)seed_count;
    out->delivered_ratio = delivered_ratio_sum / (double)seed_count;
    out->drop_ratio = drop_ratio_sum / (double)seed_count;
    return true;
}

bool irq_tune_eval_all_profiles(irq_sim_config_t cfg,
                                uint32_t packet_threshold,
                                uint32_t timer_threshold,
                                uint64_t seed_start,
                                uint64_t seed_count,
                                irq_tuning_result_t per_profile[IRQ_TRAFFIC_COUNT],
                                double *mean_reward,
                                double *worst_reward) {
    bool enabled[IRQ_TRAFFIC_COUNT];
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        enabled[t] = true;
    }
    return irq_tune_eval_profiles(cfg,
                                  enabled,
                                  packet_threshold,
                                  timer_threshold,
                                  seed_start,
                                  seed_count,
                                  per_profile,
                                  mean_reward,
                                  worst_reward);
}

bool irq_tune_eval_profiles(irq_sim_config_t cfg,
                            const bool enabled[IRQ_TRAFFIC_COUNT],
                            uint32_t packet_threshold,
                            uint32_t timer_threshold,
                            uint64_t seed_start,
                            uint64_t seed_count,
                            irq_tuning_result_t per_profile[IRQ_TRAFFIC_COUNT],
                            double *mean_reward,
                            double *worst_reward) {
    if (enabled == NULL || per_profile == NULL || mean_reward == NULL || worst_reward == NULL) {
        return false;
    }
    *mean_reward = 0.0;
    *worst_reward = DBL_MAX;
    uint32_t selected = 0u;
    for (int t = 0; t < (int)IRQ_TRAFFIC_COUNT; t++) {
        memset(&per_profile[t], 0, sizeof(per_profile[t]));
        if (!enabled[t]) {
            continue;
        }
        if (!irq_tune_eval_profile(cfg,
                                   (irq_traffic_profile_t)t,
                                   packet_threshold,
                                   timer_threshold,
                                   seed_start,
                                   seed_count,
                                   &per_profile[t])) {
            return false;
        }
        *mean_reward += per_profile[t].reward;
        selected++;
        if (per_profile[t].reward < *worst_reward) {
            *worst_reward = per_profile[t].reward;
        }
    }
    if (selected == 0u) {
        return false;
    }
    *mean_reward /= (double)selected;
    return true;
}

double irq_sim_reward(const irq_sim_t *sim) {
    if (sim == NULL) {
        return 0.0;
    }
    const irq_sim_metrics_t m = irq_sim_metrics(sim);
    return m.reward;
}

bool irq_sim_observation(const irq_sim_t *sim, double obs[IRQ_SIM_OBS_SIZE]) {
    if (sim == NULL || obs == NULL) {
        return false;
    }
    const double budget = sim->cfg.service_budget == 0u ? 1.0 : (double)sim->cfg.service_budget;
    const double cap = sim->cfg.ring_capacity == 0u ? 1.0 : (double)sim->cfg.ring_capacity;
    const double recent_avg_latency = sim->recent_latency_samples == 0u ? 0.0 : (double)sim->recent_latency_sum / (double)sim->recent_latency_samples;
    obs[0] = clamp01((double)sim->ring_count / cap);
    obs[1] = clamp01((double)sim->recent_arrivals / (budget * 2.0));
    obs[2] = clamp01((double)sim->recent_delivered / (budget * 2.0));
    obs[3] = clamp01((double)sim->recent_dropped / (budget * 2.0));
    obs[4] = clamp01(recent_avg_latency / 256.0);
    obs[5] = clamp01((double)sim->recent_max_latency / 512.0);
    obs[6] = clamp01((double)sim->cfg.packet_threshold / cap);
    obs[7] = clamp01((double)sim->cfg.timer_threshold / 512.0);
    obs[8] = clamp01((double)sim->recent_interrupts / 64.0);
    return true;
}

void irq_sim_clear_recent(irq_sim_t *sim) {
    if (sim == NULL) {
        return;
    }
    sim->recent_arrivals = 0u;
    sim->recent_delivered = 0u;
    sim->recent_dropped = 0u;
    sim->recent_interrupts = 0u;
    sim->recent_latency_sum = 0u;
    sim->recent_latency_samples = 0u;
    sim->recent_max_latency = 0u;
}

static uint64_t bandit_rng_next(uint64_t *rng) {
    uint64_t x = *rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng = x == 0u ? 0x9e3779b97f4a7c15ull : x;
    return *rng;
}

static irq_action_t bandit_choose_action(const double values[IRQ_ACTION_COUNT], const uint64_t counts[IRQ_ACTION_COUNT], uint64_t *rng) {
    if ((bandit_rng_next(rng) & 7u) == 0u) {
        return (irq_action_t)(bandit_rng_next(rng) % IRQ_ACTION_COUNT);
    }
    irq_action_t best = IRQ_ACTION_LOW_LATENCY;
    double best_value = counts[0] == 0u ? DBL_MAX : values[0];
    for (int i = 1; i < (int)IRQ_ACTION_COUNT; i++) {
        const double value = counts[i] == 0u ? DBL_MAX : values[i];
        if (value > best_value) {
            best_value = value;
            best = (irq_action_t)i;
        }
    }
    return best;
}

static void bandit_update(double values[IRQ_ACTION_COUNT], uint64_t counts[IRQ_ACTION_COUNT], irq_action_t action, double reward) {
    const int idx = (int)action;
    counts[idx] = sat_add_u64(counts[idx], 1u);
    const double n = (double)counts[idx];
    values[idx] += (reward - values[idx]) / n;
}

static bool trace_arrivals_at(const uint32_t *arrivals, uint64_t arrival_ticks, uint64_t tick, uint32_t *out) {
    if (out == NULL) {
        return false;
    }
    *out = (arrivals != NULL && tick < arrival_ticks) ? arrivals[tick] : 0u;
    return true;
}

static bool irq_sim_run_adaptive_bandit_impl(const irq_sim_config_t *cfg,
                                             const uint32_t *arrivals,
                                             uint64_t arrival_ticks,
                                             bool use_trace,
                                             irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (out == NULL || !irq_sim_reset(&sim, cfg)) {
        return false;
    }
    double values[IRQ_ACTION_COUNT] = {0.0};
    uint64_t counts[IRQ_ACTION_COUNT] = {0u};
    uint64_t rng = cfg->seed == 0u ? 0x9e3779b97f4a7c15ull : cfg->seed ^ 0xa5a5a5a5a5a5a5a5ull;
    const uint64_t control_window = 64u;
    irq_action_t action = IRQ_ACTION_BALANCED_LOW;
    (void)irq_sim_apply_action(&sim, action);
    double window_start_reward = irq_sim_reward(&sim);

    while (!sim.done) {
        uint32_t tick_arrivals = 0u;
        if ((sim.tick % control_window) == 0u && sim.tick > 0u) {
            const double window_reward = irq_sim_reward(&sim) - window_start_reward;
            bandit_update(values, counts, action, window_reward);
            action = bandit_choose_action(values, counts, &rng);
            if (!irq_sim_apply_action(&sim, action)) {
                return false;
            }
            window_start_reward = irq_sim_reward(&sim);
        }
        if (use_trace) {
            if (!trace_arrivals_at(arrivals, arrival_ticks, sim.tick, &tick_arrivals) ||
                !irq_sim_step_arrivals(&sim, tick_arrivals)) {
                return false;
            }
        } else if (!irq_sim_step(&sim)) {
            return false;
        }
    }
    const double final_window_reward = irq_sim_reward(&sim) - window_start_reward;
    bandit_update(values, counts, action, final_window_reward);
    *out = irq_sim_metrics(&sim);
    return true;
}

static bool irq_sim_run_adaptive_bandit(const irq_sim_config_t *cfg, irq_sim_metrics_t *out) {
    return irq_sim_run_adaptive_bandit_impl(cfg, NULL, 0u, false, out);
}

static bool irq_sim_run_adaptive_bandit_trace(const irq_sim_config_t *cfg,
                                              const uint32_t *arrivals,
                                              uint64_t arrival_ticks,
                                              irq_sim_metrics_t *out) {
    return irq_sim_run_adaptive_bandit_impl(cfg, arrivals, arrival_ticks, true, out);
}

static bool irq_sim_step_napi_arrivals(irq_sim_t *sim, uint32_t arrivals, bool *polling, uint32_t *idle_polls) {
    if (sim == NULL || polling == NULL || idle_polls == NULL || sim->done) {
        return false;
    }
    for (uint32_t i = 0u; i < arrivals; i++) {
        enqueue_arrival(sim);
    }

    if (*polling) {
        if (sim->ring_count > 0u) {
            service_batch(sim, false);
            *idle_polls = 0u;
        } else {
            (*idle_polls)++;
            if (*idle_polls >= 2u) {
                *polling = false;
                *idle_polls = 0u;
            }
        }
    } else if (interrupt_due(sim)) {
        service_batch(sim, true);
        *polling = true;
        *idle_polls = 0u;
    }

    record_queue_depth(sim);
    sim->tick++;
    if (sim->tick >= sim->cfg.episode_ticks) {
        sim->done = true;
    }
    return true;
}

static bool irq_sim_run_napi_polling(const irq_sim_config_t *cfg,
                                     const uint32_t *arrivals,
                                     uint64_t arrival_ticks,
                                     bool use_trace,
                                     irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (out == NULL || !irq_sim_reset(&sim, cfg) || !irq_sim_apply_baseline(&sim, IRQ_POLICY_NAPI_POLLING)) {
        return false;
    }
    bool polling = false;
    uint32_t idle_polls = 0u;
    while (!sim.done) {
        uint32_t tick_arrivals = 0u;
        if (use_trace) {
            if (!trace_arrivals_at(arrivals, arrival_ticks, sim.tick, &tick_arrivals)) {
                return false;
            }
        } else {
            tick_arrivals = traffic_arrivals(&sim);
        }
        if (!irq_sim_step_napi_arrivals(&sim, tick_arrivals, &polling, &idle_polls)) {
            return false;
        }
    }
    *out = irq_sim_metrics(&sim);
    return true;
}

bool irq_sim_run_baseline(const irq_sim_config_t *cfg, irq_baseline_policy_t policy, irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (policy == IRQ_POLICY_ADAPTIVE_BANDIT) {
        return irq_sim_run_adaptive_bandit(cfg, out);
    }
    if (policy == IRQ_POLICY_NAPI_POLLING) {
        return irq_sim_run_napi_polling(cfg, NULL, 0u, false, out);
    }
    if (out == NULL || !irq_sim_reset(&sim, cfg) || !irq_sim_apply_baseline(&sim, policy)) {
        return false;
    }
    while (!sim.done) {
        if (policy == IRQ_POLICY_SIMPLE_ADAPTIVE && (sim.tick % 32u) == 0u) {
            (void)irq_sim_apply_baseline(&sim, policy);
        }
        (void)irq_sim_step(&sim);
    }
    *out = irq_sim_metrics(&sim);
    return true;
}

bool irq_sim_run_baseline_trace(const irq_sim_config_t *cfg,
                                irq_baseline_policy_t policy,
                                const uint32_t *arrivals,
                                uint64_t arrival_ticks,
                                irq_sim_metrics_t *out) {
    irq_sim_t sim;
    if (policy == IRQ_POLICY_ADAPTIVE_BANDIT) {
        return irq_sim_run_adaptive_bandit_trace(cfg, arrivals, arrival_ticks, out);
    }
    if (policy == IRQ_POLICY_NAPI_POLLING) {
        return irq_sim_run_napi_polling(cfg, arrivals, arrival_ticks, true, out);
    }
    if (out == NULL || !irq_sim_reset(&sim, cfg) || !irq_sim_apply_baseline(&sim, policy)) {
        return false;
    }
    while (!sim.done) {
        uint32_t tick_arrivals = 0u;
        if (policy == IRQ_POLICY_SIMPLE_ADAPTIVE && (sim.tick % 32u) == 0u) {
            (void)irq_sim_apply_baseline(&sim, policy);
        }
        if (!trace_arrivals_at(arrivals, arrival_ticks, sim.tick, &tick_arrivals) ||
            !irq_sim_step_arrivals(&sim, tick_arrivals)) {
            return false;
        }
    }
    *out = irq_sim_metrics(&sim);
    return true;
}
