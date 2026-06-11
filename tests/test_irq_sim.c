#include "irq_sim.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void require_true(bool ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

static irq_sim_metrics_t run_cfg(irq_sim_config_t cfg, irq_baseline_policy_t policy) {
    irq_sim_metrics_t m;
    require_true(irq_sim_run_baseline(&cfg, policy, &m), "baseline run");
    require_true(isfinite(m.reward), "finite reward");
    return m;
}

static irq_sim_metrics_t run_direct(irq_sim_config_t cfg) {
    irq_sim_t sim;
    require_true(irq_sim_reset(&sim, &cfg), "direct reset");
    while (!sim.done) {
        (void)irq_sim_step(&sim);
    }
    irq_sim_metrics_t m = irq_sim_metrics(&sim);
    require_true(isfinite(m.reward), "finite direct reward");
    return m;
}

static bool fixed_action_selector(const irq_sim_t *sim, const double obs[IRQ_SIM_OBS_SIZE], void *ctx, irq_action_t *action) {
    (void)sim;
    (void)obs;
    *action = *(const irq_action_t *)ctx;
    return true;
}

static void test_reproducible(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_BURSTY;
    cfg.seed = 42u;
    irq_sim_metrics_t a = run_cfg(cfg, IRQ_POLICY_FIXED_BALANCED);
    irq_sim_metrics_t b = run_cfg(cfg, IRQ_POLICY_FIXED_BALANCED);
    require_true(a.offered == b.offered, "offered reproducible");
    require_true(a.delivered == b.delivered, "delivered reproducible");
    require_true(a.dropped == b.dropped, "dropped reproducible");
    require_true(a.interrupts == b.interrupts, "interrupts reproducible");
    require_true(a.p99_latency == b.p99_latency, "latency reproducible");
}

static void test_light_load_no_loss(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_LOW;
    cfg.ring_capacity = 256u;
    cfg.service_budget = 64u;
    cfg.packet_threshold = 1u;
    cfg.timer_threshold = 0u;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_CPU_LIMITED);
    require_true(m.dropped == 0u, "no loss under light load");
    require_true(m.final_queue_depth == 0u, "light load drains queue");
}

static void test_overflow_drops(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_OVERLOAD_SPIKE;
    cfg.ring_capacity = 8u;
    cfg.service_budget = 1u;
    cfg.packet_threshold = 8u;
    cfg.timer_threshold = 100u;
    cfg.episode_ticks = 1000u;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_FIXED_THROUGHPUT);
    require_true(m.dropped > 0u, "drops under overflow");
}

static void test_interrupt_counts(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.episode_ticks = 3000u;
    irq_sim_metrics_t no_coal = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_ORACLE);
    irq_sim_metrics_t cpu_limited = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_CPU_LIMITED);
    irq_sim_metrics_t throughput = run_cfg(cfg, IRQ_POLICY_FIXED_THROUGHPUT);
    require_true(no_coal.interrupts > throughput.interrupts, "no coalescing has more interrupts");
    require_true(no_coal.interrupts == no_coal.delivered, "oracle no coalescing interrupts per delivered packet");
    require_true(cpu_limited.interrupts <= cpu_limited.delivered, "cpu-limited no coalescing batches service budget");
}

static void test_baseline_comparison_under_overload(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_OVERLOAD_SPIKE;
    cfg.ring_capacity = 64u;
    cfg.service_budget = 4u;
    cfg.episode_ticks = 1200u;
    irq_sim_metrics_t oracle = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_ORACLE);
    irq_sim_metrics_t cpu_limited = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_CPU_LIMITED);
    irq_sim_metrics_t balanced = run_cfg(cfg, IRQ_POLICY_FIXED_BALANCED);
    irq_sim_metrics_t throughput = run_cfg(cfg, IRQ_POLICY_FIXED_THROUGHPUT);
    require_true(oracle.delivered >= cpu_limited.delivered, "oracle delivers at least cpu-limited");
    require_true(cpu_limited.dropped > 0u, "cpu-limited no coalescing can drop under overload");
    require_true(balanced.offered == cpu_limited.offered, "balanced sees same offered load");
    require_true(throughput.offered == cpu_limited.offered, "throughput sees same offered load");
    require_true(cpu_limited.interrupts <= cfg.episode_ticks, "cpu-limited baseline has at most one interrupt per tick");
}

static void test_timer_fires_below_threshold(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_LOW;
    cfg.packet_threshold = 64u;
    cfg.timer_threshold = 3u;
    irq_sim_t sim;
    require_true(irq_sim_reset(&sim, &cfg), "reset");
    while (!sim.done) {
        (void)irq_sim_step(&sim);
    }
    irq_sim_metrics_t m = irq_sim_metrics(&sim);
    require_true(m.interrupts > 0u, "timer interrupt fires");
    require_true(m.p95_latency <= 8.0, "timer bounds sparse latency");
}

static void test_packet_threshold_before_timer(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.packet_threshold = 2u;
    cfg.timer_threshold = 1000u;
    cfg.episode_ticks = 500u;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_FIXED_LOW_LATENCY);
    require_true(m.interrupts > 0u, "packet threshold fires");
    require_true(m.p95_latency < 1000.0, "threshold beats long timer");
}

static void test_timer_increases_latency(void) {
    irq_sim_config_t low = irq_sim_default_config();
    low.traffic_profile = IRQ_TRAFFIC_STEADY_LOW;
    low.packet_threshold = 64u;
    low.timer_threshold = 2u;
    irq_sim_config_t high = low;
    high.timer_threshold = 50u;
    irq_sim_metrics_t a = run_direct(low);
    irq_sim_metrics_t b = run_direct(high);
    require_true(b.avg_latency > a.avg_latency, "larger timer increases latency");
}

static void test_adaptive_changes(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_OVERLOAD_SPIKE;
    cfg.episode_ticks = 2000u;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_SIMPLE_ADAPTIVE);
    require_true(m.setting_changes > 1u, "adaptive changes settings");
}

static void test_adaptive_bandit_deterministic(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_BURSTY;
    cfg.episode_ticks = 1500u;
    cfg.seed = 77u;
    irq_sim_metrics_t a = run_cfg(cfg, IRQ_POLICY_ADAPTIVE_BANDIT);
    irq_sim_metrics_t b = run_cfg(cfg, IRQ_POLICY_ADAPTIVE_BANDIT);
    require_true(a.offered == b.offered, "bandit offered reproducible");
    require_true(a.delivered == b.delivered, "bandit delivered reproducible");
    require_true(a.dropped == b.dropped, "bandit dropped reproducible");
    require_true(a.interrupts == b.interrupts, "bandit interrupts reproducible");
    require_true(a.setting_changes > 0u, "bandit changes settings");
    require_true(isfinite(a.reward), "bandit reward finite");
}

static void test_wraparound_accounting(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.ring_capacity = 5u;
    cfg.service_budget = 3u;
    cfg.packet_threshold = 3u;
    cfg.timer_threshold = 2u;
    cfg.episode_ticks = 2000u;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_FIXED_LOW_LATENCY);
    require_true(m.offered == m.delivered + m.dropped + m.final_queue_depth, "packet accounting holds");
}

static void test_zero_idle_profile(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_ZERO_IDLE;
    irq_sim_metrics_t m = run_cfg(cfg, IRQ_POLICY_FIXED_BALANCED);
    require_true(m.offered == 0u, "zero idle offers no packets");
    require_true(m.delivered == 0u, "zero idle delivers no packets");
    require_true(m.dropped == 0u, "zero idle drops no packets");
    require_true(m.interrupts == 0u, "zero idle has no interrupts");
    require_true(m.final_queue_depth == 0u, "zero idle final queue empty");
    require_true(m.reward == 0.0, "zero idle reward neutral");
}

static void test_rl_api(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    irq_sim_t sim;
    irq_sim_t sim_a;
    irq_sim_t sim_b;
    double obs[IRQ_SIM_OBS_SIZE];
    double reward = 0.0;
    double reward_a = 0.0;
    double reward_b = 0.0;
    bool done = false;
    require_true(irq_sim_reset(&sim, &cfg), "rl reset");
    require_true(!irq_sim_apply_action(&sim, (irq_action_t)999), "invalid action rejected");
    require_true(irq_sim_apply_action(&sim, IRQ_ACTION_BULK), "valid action accepted");
    for (int i = 0; i < 20; i++) {
        (void)irq_sim_step(&sim);
    }
    require_true(irq_sim_observation(&sim, obs), "observation");
    for (size_t i = 0; i < IRQ_SIM_OBS_SIZE; i++) {
        require_true(isfinite(obs[i]), "obs finite");
        require_true(obs[i] >= 0.0 && obs[i] <= 1.0, "obs bounded");
    }
    require_true(isfinite(irq_sim_reward(&sim)), "reward finite");
    require_true(irq_sim_reset(&sim, &cfg), "reset clears");
    require_true(sim.tick == 0u && sim.delivered == 0u && sim.ring_count == 0u, "reset state clear");
    require_true(irq_sim_episode_step(&sim, IRQ_ACTION_BALANCED_LOW, obs, &reward, &done), "episode step");
    require_true(isfinite(reward), "step reward finite");
    require_true(!done, "not done after one step");
    for (size_t i = 0; i < IRQ_SIM_OBS_SIZE; i++) {
        require_true(isfinite(obs[i]), "step obs finite");
        require_true(obs[i] >= 0.0 && obs[i] <= 1.0, "step obs bounded");
    }
    require_true(irq_sim_reset(&sim_a, &cfg), "reset a");
    require_true(irq_sim_reset(&sim_b, &cfg), "reset b");
    for (int i = 0; i < 64; i++) {
        require_true(irq_sim_episode_step(&sim_a, IRQ_ACTION_BALANCED_HIGH, NULL, &reward_a, &done), "step a");
        require_true(irq_sim_episode_step(&sim_b, IRQ_ACTION_BALANCED_HIGH, NULL, &reward_b, &done), "step b");
        require_true(reward_a == reward_b, "step reward reproducible");
    }
    require_true(irq_sim_metrics(&sim_a).delivered == irq_sim_metrics(&sim_b).delivered, "step metrics reproducible");
}

static void test_control_loop_fixed_interval(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_BURSTY;
    cfg.packet_threshold = 4u;
    cfg.timer_threshold = 8u;
    irq_sim_metrics_t direct = run_direct(cfg);

    cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_BURSTY;
    irq_action_t action = IRQ_ACTION_BALANCED_LOW;
    irq_sim_metrics_t controlled;
    require_true(irq_sim_run_control_loop(&cfg, 32u, fixed_action_selector, &action, &controlled), "control loop run");
    require_true(controlled.offered == direct.offered, "control loop offered matches direct");
    require_true(controlled.delivered == direct.delivered, "control loop delivered matches direct");
    require_true(controlled.dropped == direct.dropped, "control loop dropped matches direct");
    require_true(controlled.interrupts == direct.interrupts, "control loop interrupts matches direct");
    require_true(controlled.setting_changes == 0u, "fixed repeated action has no runtime changes");
    require_true(controlled.reward == direct.reward, "control loop reward matches direct");

    require_true(!irq_sim_run_control_loop(&cfg, 0u, fixed_action_selector, &action, &controlled), "zero interval rejected");
    require_true(!irq_sim_run_control_loop(&cfg, 32u, NULL, &action, &controlled), "null selector rejected");
    require_true(!irq_sim_run_control_loop(&cfg, 32u, fixed_action_selector, &action, NULL), "null metrics rejected");
}

static void test_config_validation(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    char err[128];
    require_true(irq_sim_validate_config(&cfg, err, sizeof(err)), "default config valid");
    cfg.ring_capacity = 0u;
    require_true(!irq_sim_validate_config(&cfg, err, sizeof(err)), "zero ring invalid");
    cfg = irq_sim_default_config();
    cfg.service_budget = cfg.ring_capacity + 1u;
    require_true(!irq_sim_validate_config(&cfg, err, sizeof(err)), "oversized budget invalid");
    cfg = irq_sim_default_config();
    cfg.packet_threshold = 0u;
    require_true(!irq_sim_validate_config(&cfg, err, sizeof(err)), "zero packet threshold invalid");
    cfg = irq_sim_default_config();
    cfg.timer_threshold = IRQ_SIM_LATENCY_HIST;
    require_true(!irq_sim_validate_config(&cfg, err, sizeof(err)), "oversized timer invalid");
    cfg = irq_sim_default_config();
    cfg.episode_ticks = IRQ_SIM_MAX_TICKS + 1u;
    require_true(!irq_sim_validate_config(&cfg, err, sizeof(err)), "oversized episode invalid");
}

static void test_unresolved_queue_penalty(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.episode_ticks = 500u;
    cfg.packet_threshold = 64u;
    cfg.timer_threshold = 64u;
    irq_sim_metrics_t queued = run_cfg(cfg, IRQ_POLICY_FIXED_THROUGHPUT);
    cfg.packet_threshold = 1u;
    cfg.timer_threshold = 0u;
    irq_sim_metrics_t drained = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_ORACLE);
    require_true(queued.final_queue_depth > 0u, "queued packets exist");
    require_true(drained.final_queue_depth == 0u, "no coalescing drains packets");
    require_true(queued.reward < drained.reward, "unresolved queue lowers reward");
}

static void test_edge_configs(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.ring_capacity = IRQ_SIM_MAX_RING;
    cfg.service_budget = IRQ_SIM_MAX_RING;
    cfg.packet_threshold = IRQ_SIM_MAX_RING;
    cfg.timer_threshold = 0u;
    cfg.episode_ticks = 1u;
    irq_sim_metrics_t max_ring = run_direct(cfg);
    require_true(max_ring.offered == max_ring.delivered + max_ring.dropped + max_ring.final_queue_depth, "max ring accounting");

    cfg = irq_sim_default_config();
    cfg.ring_capacity = 1u;
    cfg.service_budget = 1u;
    cfg.packet_threshold = 1u;
    cfg.timer_threshold = 0u;
    cfg.episode_ticks = 500u;
    irq_sim_metrics_t ring_one = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_CPU_LIMITED);
    require_true(ring_one.max_queue_depth <= 1u, "ring capacity one respected");
    require_true(ring_one.offered == ring_one.delivered + ring_one.dropped + ring_one.final_queue_depth, "ring one accounting");

    cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.service_budget = 1u;
    cfg.packet_threshold = cfg.ring_capacity;
    cfg.timer_threshold = 0u;
    irq_sim_metrics_t budget_one = run_direct(cfg);
    require_true(budget_one.avg_batch_size <= 1.0, "service budget one bounds batch size");
}

static void test_reward_components(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_OVERLOAD_SPIKE;
    cfg.ring_capacity = 16u;
    cfg.service_budget = 1u;
    cfg.episode_ticks = 1000u;
    irq_sim_metrics_t dropping = run_cfg(cfg, IRQ_POLICY_FIXED_THROUGHPUT);
    require_true(isfinite(dropping.reward_components.total), "reward components finite");
    require_true(dropping.reward_components.drop_penalty > dropping.reward_components.interrupt_cost_penalty, "drops dominate interrupt penalty under overload");

    cfg = irq_sim_default_config();
    cfg.traffic_profile = IRQ_TRAFFIC_STEADY_HIGH;
    cfg.packet_threshold = 64u;
    cfg.timer_threshold = 64u;
    cfg.episode_ticks = 500u;
    irq_sim_metrics_t queued = run_direct(cfg);
    require_true(queued.final_queue_depth > 0u, "queued final depth exists");
    require_true(queued.reward_components.unresolved_queue_penalty > 0.0, "unresolved queue penalized");

    cfg = irq_sim_default_config();
    irq_sim_metrics_t irq_heavy = run_cfg(cfg, IRQ_POLICY_NO_COALESCING_ORACLE);
    require_true(irq_heavy.reward_components.interrupt_cost_penalty > 0.0, "interrupt-heavy mode pays irq penalty");
}

static void test_scenarios(void) {
    require_true(irq_scenario_count() >= 7u, "scenario suite present");
    const irq_scenario_t *s = irq_scenario_by_name("small_rx_ring_stress");
    require_true(s != NULL, "scenario lookup by name");
    irq_sim_metrics_t m = run_cfg(s->cfg, IRQ_POLICY_FIXED_BALANCED);
    require_true(isfinite(m.reward), "scenario run finite");
    require_true(irq_scenario_by_index(irq_scenario_count()) == NULL, "scenario index bounds");
}

static void test_tuning_eval_deterministic(void) {
    irq_sim_config_t cfg = irq_sim_default_config();
    cfg.episode_ticks = 1000u;
    irq_tuning_result_t fast_a;
    irq_tuning_result_t fast_b;
    irq_tuning_result_t slow;
    require_true(irq_tune_eval_profile(cfg, IRQ_TRAFFIC_STEADY_LOW, 1u, 0u, 1u, 4u, &fast_a), "tune eval fast a");
    require_true(irq_tune_eval_profile(cfg, IRQ_TRAFFIC_STEADY_LOW, 1u, 0u, 1u, 4u, &fast_b), "tune eval fast b");
    require_true(irq_tune_eval_profile(cfg, IRQ_TRAFFIC_STEADY_LOW, 64u, 64u, 1u, 4u, &slow), "tune eval slow");
    require_true(fast_a.reward == fast_b.reward, "tune eval deterministic reward");
    require_true(fast_a.interrupts == fast_b.interrupts, "tune eval deterministic interrupts");
    require_true(fast_a.reward > slow.reward, "tune eval ranks low-latency candidate higher for sparse traffic");

    irq_tuning_result_t per_profile[IRQ_TRAFFIC_COUNT];
    double mean = 0.0;
    double worst = 0.0;
    require_true(irq_tune_eval_all_profiles(cfg, 1u, 0u, 1u, 2u, per_profile, &mean, &worst), "tune eval all profiles");
    require_true(isfinite(mean), "mean reward finite");
    require_true(isfinite(worst), "worst reward finite");
    require_true(per_profile[IRQ_TRAFFIC_ZERO_IDLE].reward == 0.0, "zero idle tuning neutral");
}

int main(void) {
    test_config_validation();
    test_reproducible();
    test_light_load_no_loss();
    test_overflow_drops();
    test_interrupt_counts();
    test_baseline_comparison_under_overload();
    test_timer_fires_below_threshold();
    test_packet_threshold_before_timer();
    test_timer_increases_latency();
    test_adaptive_changes();
    test_adaptive_bandit_deterministic();
    test_wraparound_accounting();
    test_zero_idle_profile();
    test_rl_api();
    test_control_loop_fixed_interval();
    test_unresolved_queue_penalty();
    test_edge_configs();
    test_reward_components();
    test_scenarios();
    test_tuning_eval_deterministic();
    puts("ok");
    return 0;
}
