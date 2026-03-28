SHELL := /bin/bash

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -mcx16 -pthread
CPPFLAGS ?= -I../.. -Isrc
LDFLAGS ?=
LDLIBS ?= -latomic -ltbb

SRC := test/bench_queues.cpp
TEST_SRC := test/test_lock_free_queue.cpp
TSAN_TEST_SRC := test/test_simple_mc_tsan.cpp
OUT_DIR := build
DEP_DIR := $(OUT_DIR)/deps
TEST_BIN := $(OUT_DIR)/test_lock_free_queue
TSAN_TEST_BIN := $(OUT_DIR)/test_simple_mc_tsan
TSAN_BENCH_BINS = $(addprefix $(OUT_DIR)/bench_tsan_,$(QUEUES))
ASAN_BENCH_BINS = $(addprefix $(OUT_DIR)/bench_asan_,$(QUEUES))
UBSAN_BENCH_BINS = $(addprefix $(OUT_DIR)/bench_ubsan_,$(QUEUES))

TEST_LDLIBS ?= -latomic
TSAN_CXXFLAGS ?= -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread
TSAN_LDLIBS ?= -fsanitize=thread -latomic
TSAN_BENCH_CXXFLAGS ?= -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=thread -Wno-tsan -pthread
TSAN_BENCH_LDLIBS ?= -fsanitize=thread -latomic
TSAN_BENCH_ARGS ?= -p 4 -c 4 -n 50000 -r 1
TSAN_ALLOW_FAIL_QUEUES ?= tbb
ASAN_BENCH_CXXFLAGS ?= -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address -pthread
ASAN_BENCH_LDLIBS ?= -fsanitize=address -latomic
ASAN_BENCH_ARGS ?= -p 4 -c 4 -n 50000 -r 1
ASAN_OPTIONS_BENCH ?= halt_on_error=1:detect_leaks=0
ASAN_RUNTIME_LIB ?= $(shell $(CXX) -print-file-name=libasan.so)
UBSAN_BENCH_CXXFLAGS ?= -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize-recover=all -pthread
UBSAN_BENCH_LDLIBS ?= -fsanitize=undefined -latomic
UBSAN_BENCH_ARGS ?= -p 4 -c 4 -n 50000 -r 1
SAN_TBB_EXTRA_LDLIBS ?= -ltbb
SAN_CLEAN_ENV ?= env -u LD_LIBRARY_PATH -u LD_PRELOAD -u TSAN_OPTIONS -u ASAN_OPTIONS -u UBSAN_OPTIONS

QUEUES := mutex_queue two_lock cas_lf dvyukov_mpmc moodycamel simple_mc tbb
DEPS := $(patsubst %,$(DEP_DIR)/%.d,$(QUEUES))

mutex_queue_DEF := USE_MUTEX_QUEUE
two_lock_DEF := USE_TWO_LOCK
cas_lf_DEF := USE_CAS_LF
dvyukov_mpmc_DEF := USE_DVYUKOV_MPMC
moodycamel_DEF := USE_MOODYCAMEL
simple_mc_DEF := USE_SIMPLE_MC
tbb_DEF := USE_TBB

SCENARIO_ARGS ?= -p 8 -c 8 -n 200000 -r 5
SCENARIOS ?= 1:1:1P-1C 4:4:4P-4C 1:4:1P-4C 4:1:4P-1C 8:8:8P-8C
ITEMS ?= 200000
ROUNDS ?= 5

.PHONY: all clean run run-% list test test-tsan test-tsan-bench test-tsan-all test-asan-bench test-ubsan-bench test-sanitize-all

all: $(addprefix $(OUT_DIR)/bench_,$(QUEUES))

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(DEP_DIR): | $(OUT_DIR)
	mkdir -p $(DEP_DIR)

$(OUT_DIR)/bench_%: $(SRC) | $(OUT_DIR) $(DEP_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d \
		-D$($*_DEF) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(OUT_DIR)/bench_tsan_%: $(SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(TSAN_BENCH_CXXFLAGS) -D$($*_DEF) -o $@ $< \
		$(LDFLAGS) $(TSAN_BENCH_LDLIBS) $(if $(filter tbb,$*),$(SAN_TBB_EXTRA_LDLIBS),)

$(OUT_DIR)/bench_asan_%: $(SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(ASAN_BENCH_CXXFLAGS) -D$($*_DEF) -o $@ $< \
		$(LDFLAGS) $(ASAN_BENCH_LDLIBS) $(if $(filter tbb,$*),$(SAN_TBB_EXTRA_LDLIBS),)

$(OUT_DIR)/bench_ubsan_%: $(SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(UBSAN_BENCH_CXXFLAGS) -D$($*_DEF) -o $@ $< \
		$(LDFLAGS) $(UBSAN_BENCH_LDLIBS) $(if $(filter tbb,$*),$(SAN_TBB_EXTRA_LDLIBS),)

run: all
	@printf "\n%-8s %-20s %9s %9s %9s %10s %14s %6s\n" \
		"Scene" "Queue" "Real(ms)" "User(ms)" "Sys(ms)" "RSS(KB)" "Ops/sec" "Check"
	@printf "%-8s %-20s %9s %9s %9s %10s %14s %6s\n" \
		"------" "--------------------" "--------" "--------" "--------" "---------" "-------------" "-----"
	@for scenario in $(SCENARIOS); do \
		np=$${scenario%%:*}; \
		rest=$${scenario#*:}; \
		nc=$${rest%%:*}; \
		label=$${rest#*:}; \
		for q in $(QUEUES); do \
			result=$$(./$(OUT_DIR)/bench_$$q -p $$np -c $$nc -n $(ITEMS) -r $(ROUNDS)); \
			IFS=$$'\t' read -r name wall user sys rss ops ok <<< "$$result"; \
			printf "%-8s %-20s %9s %9s %9s %10s %14s %6s\n" \
				"$$label" "$$name" "$$wall" "$$user" "$$sys" "$$rss" "$$ops" "$$ok"; \
		done; \
		echo ""; \
	done

run-%: $(OUT_DIR)/bench_%
	$< $(SCENARIO_ARGS)

list:
	@printf '%s\n' $(QUEUES)

$(TEST_BIN): $(TEST_SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(TEST_LDLIBS)

test: $(TEST_BIN)
	$<

$(TSAN_TEST_BIN): $(TSAN_TEST_SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(TSAN_CXXFLAGS) -o $@ $< $(LDFLAGS) $(TSAN_LDLIBS)

test-tsan: $(TSAN_TEST_BIN)
	TSAN_OPTIONS="halt_on_error=1" $<

test-tsan-bench: $(TSAN_BENCH_BINS)
	@failed=0; \
	for q in $(QUEUES); do \
		echo "== TSAN bench: $$q =="; \
		if $(SAN_CLEAN_ENV) TSAN_OPTIONS="halt_on_error=1" ./$(OUT_DIR)/bench_tsan_$$q $(TSAN_BENCH_ARGS); then \
			echo "[PASS] $$q"; \
		else \
			if echo " $(TSAN_ALLOW_FAIL_QUEUES) " | grep -q " $$q "; then \
				echo "[ALLOW_FAIL] $$q"; \
			else \
				echo "[FAIL] $$q"; \
				failed=1; \
			fi; \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "TSAN bench found races in one or more queues."; \
		exit 1; \
	fi

test-tsan-all: test-tsan test-tsan-bench

test-asan-bench: $(ASAN_BENCH_BINS)
	@failed=0; \
	for q in $(QUEUES); do \
		echo "== ASAN bench: $$q =="; \
		if $(SAN_CLEAN_ENV) \
		   LD_PRELOAD="$(ASAN_RUNTIME_LIB)" \
		   ASAN_OPTIONS="$(ASAN_OPTIONS_BENCH)" \
		   ./$(OUT_DIR)/bench_asan_$$q $(ASAN_BENCH_ARGS); then \
			echo "[PASS] $$q"; \
		else \
			echo "[FAIL] $$q"; \
			failed=1; \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "ASAN bench found issues in one or more queues."; \
		exit 1; \
	fi

test-ubsan-bench: $(UBSAN_BENCH_BINS)
	@failed=0; \
	for q in $(QUEUES); do \
		echo "== UBSAN bench: $$q =="; \
		if $(SAN_CLEAN_ENV) UBSAN_OPTIONS="halt_on_error=1" ./$(OUT_DIR)/bench_ubsan_$$q $(UBSAN_BENCH_ARGS); then \
			echo "[PASS] $$q"; \
		else \
			echo "[FAIL] $$q"; \
			failed=1; \
		fi; \
	done; \
	if [ $$failed -ne 0 ]; then \
		echo "UBSAN bench found issues in one or more queues."; \
		exit 1; \
	fi

test-sanitize-all: test-tsan-all test-asan-bench test-ubsan-bench

clean:
	rm -rf $(OUT_DIR)

-include $(DEPS)
