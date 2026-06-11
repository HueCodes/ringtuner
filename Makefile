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

.PHONY: all test run tune clean asan dirs

all: $(APP)

dirs:
	mkdir -p $(BUILD)

$(BUILD)/irq_sim.o: src/irq_sim.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/irq_sim.c -o $@

$(BUILD)/main.o: src/main.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/main.c -o $@

$(BUILD)/tune.o: src/tune.c src/irq_sim.h | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/tune.c -o $@

$(APP): $(SIM_OBJS) $(BUILD)/main.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(TUNE): $(SIM_OBJS) $(BUILD)/tune.o
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

asan: | dirs
	$(CC) $(CPPFLAGS) $(DBGFLAGS) src/irq_sim.c tests/test_irq_sim.c $(LDLIBS) -o $(BUILD)/test_irq_sim_asan
	$(BUILD)/test_irq_sim_asan

clean:
	rm -rf $(BUILD)
