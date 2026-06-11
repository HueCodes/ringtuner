CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2
DBGFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O0 -g -fsanitize=address,undefined
CPPFLAGS ?= -Isrc
LDLIBS ?= -lm
BUILD := build

SIM_OBJS := $(BUILD)/irq_sim.o
APP := $(BUILD)/ringtuner
TEST := $(BUILD)/test_irq_sim
TUNE := $(BUILD)/tune
COMPARE := $(BUILD)/compare
PARETO := $(BUILD)/pareto
REPORT := $(BUILD)/report

.PHONY: all test run tune tune-scenario tune-scenarios tune-scenario-traffic compare pareto trace report clean asan dirs

all: $(APP)

dirs:
	mkdir -p $(BUILD)

$(BUILD)/irq_sim.o: src/irq_sim.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/irq_sim.c -o $@

$(BUILD)/main.o: src/main.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/main.c -o $@

$(BUILD)/tune.o: src/tune.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tune.c -o $@

$(BUILD)/compare.o: src/compare.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/compare.c -o $@

$(BUILD)/pareto.o: src/pareto.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/pareto.c -o $@

$(BUILD)/report.o: src/report.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/report.c -o $@

$(APP): $(SIM_OBJS) $(BUILD)/main.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(TUNE): $(SIM_OBJS) $(BUILD)/tune.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(COMPARE): $(SIM_OBJS) $(BUILD)/compare.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(PARETO): $(SIM_OBJS) $(BUILD)/pareto.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(REPORT): $(BUILD)/report.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_irq_sim.o: tests/test_irq_sim.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c tests/test_irq_sim.c -o $@

$(TEST): $(SIM_OBJS) $(BUILD)/test_irq_sim.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

test: $(TEST)
	$(TEST)

run: $(APP)
	$(APP)

tune: $(TUNE)
	$(TUNE) --csv results/tuning-grid.csv

tune-scenario: $(TUNE)
	$(TUNE) --scenario small_rx_ring_stress --csv results/tuning-small-ring.csv

tune-scenarios: $(TUNE)
	$(TUNE) --scenario all --csv results/tuning-scenarios.csv

tune-scenario-traffic: $(TUNE)
	$(TUNE) --scenario all --traffic scenario --csv results/tuning-scenario-traffic.csv

compare: $(COMPARE)
	$(COMPARE) --csv results/comparison.csv

pareto: $(PARETO)
	$(PARETO) --scenario all --traffic scenario --csv results/pareto.csv

trace: $(APP)
	$(APP) --trace traces/microburst.csv --policy fixed_balanced --csv results/trace-microburst.csv

report: test $(APP) $(TUNE) $(COMPARE) $(PARETO) $(REPORT)
	mkdir -p results
	$(APP) --csv results/baselines.csv
	$(APP) --scenario all --csv results/scenarios.csv
	$(APP) --trace traces/microburst.csv --policy fixed_balanced --csv results/trace-microburst.csv
	$(TUNE) --mode per-profile --train-seeds 16 --eval-seeds 16 --csv results/tuning-grid.csv
	$(TUNE) --scenario small_rx_ring_stress --train-seeds 16 --eval-seeds 16 --csv results/tuning-small-ring.csv
	$(TUNE) --scenario all --train-seeds 16 --eval-seeds 16 --csv results/tuning-scenarios.csv
	$(TUNE) --scenario all --traffic scenario --train-seeds 16 --eval-seeds 16 --csv results/tuning-scenario-traffic.csv
	$(COMPARE) --train-seeds 16 --eval-seeds 16 --csv results/comparison.csv
	$(PARETO) --scenario all --traffic scenario --csv results/pareto.csv
	$(REPORT) --out results/report.md

asan: | dirs
	$(CC) $(CPPFLAGS) $(DBGFLAGS) src/irq_sim.c tests/test_irq_sim.c $(LDLIBS) -o $(BUILD)/test_irq_sim_asan
	$(BUILD)/test_irq_sim_asan

clean:
	rm -rf $(BUILD)
