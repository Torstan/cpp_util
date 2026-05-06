SHELL := /bin/bash

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -mcx16 -pthread
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
LDLIBS ?= -latomic

SRC := benchmarks/bench_queues.cpp
TEST_SRC := tests/test_queues.cpp
TSAN_TEST_SRC := tests/test_simple_concurrent_queue_tsan.cpp
OUT_DIR := build
DEP_DIR := $(OUT_DIR)/deps
RUN_STAGE_DIR := $(OUT_DIR)/run_stage
TEST_BIN := $(OUT_DIR)/test_lock_free_queue
TSAN_TEST_BIN := $(OUT_DIR)/test_simple_concurrent_queue_tsan
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
RUN_STAGED := bash scripts/run_staged_binary.sh $(RUN_STAGE_DIR)

DEFAULT_QUEUES := mutex_queue two_lock_queue lock_free_queue vyukov_bounded_queue sharded_vyukov_queue moodycamel simple_concurrent_queue
OPTIONAL_QUEUES := tbb
QUEUES := $(DEFAULT_QUEUES) $(OPTIONAL_QUEUES)
DEPS := $(patsubst %,$(DEP_DIR)/%.d,$(QUEUES))

mutex_queue_DEF := USE_MUTEX_QUEUE
two_lock_queue_DEF := USE_TWO_LOCK_QUEUE
lock_free_queue_DEF := USE_LOCK_FREE_QUEUE
vyukov_bounded_queue_DEF := USE_VYUKOV_BOUNDED_QUEUE
sharded_vyukov_queue_DEF := USE_SHARDED_VYUKOV_QUEUE
moodycamel_DEF := USE_MOODYCAMEL
simple_concurrent_queue_DEF := USE_SIMPLE_CONCURRENT_QUEUE
tbb_DEF := USE_TBB
tbb_LDLIBS := -ltbb

SCENARIO_ARGS ?= -p 8 -c 8 -n 200000 -r 5
SCENARIOS ?= 1:1:1P-1C 4:4:4P-4C 1:4:1P-4C 4:1:4P-1C 8:8:8P-8C
ITEMS ?= 200000
ROUNDS ?= 5

.PHONY: all all-with-tbb clean run run-% list test test-tsan test-tsan-bench test-tsan-all test-asan-bench test-ubsan-bench test-sanitize-all

all: $(addprefix $(OUT_DIR)/bench_,$(DEFAULT_QUEUES))

all-with-tbb: all $(OUT_DIR)/bench_tbb

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(DEP_DIR): | $(OUT_DIR)
	mkdir -p $(DEP_DIR)

$(OUT_DIR)/bench_%: $(SRC) | $(OUT_DIR) $(DEP_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d \
		-D$($*_DEF) -o $@ $< $(LDFLAGS) $(LDLIBS) $($*_LDLIBS)

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
	@printf "%-8s %-21s %9s %9s %9s %10s %14s %6s\n" \
		"------" "--------------------" "--------" "--------" "--------" "---------" "-------------" "-----"
	@for scenario in $(SCENARIOS); do \
		np=$${scenario%%:*}; \
		rest=$${scenario#*:}; \
		nc=$${rest%%:*}; \
		label=$${rest#*:}; \
		for q in $(DEFAULT_QUEUES); do \
			status=0; \
			result=$$($(RUN_STAGED) ./$(OUT_DIR)/bench_$$q -p $$np -c $$nc -n $(ITEMS) -r $(ROUNDS)) || status=$$?; \
			if [ $$status -ne 0 ]; then \
				exit $$status; \
			fi; \
			IFS=$$'\t' read -r name wall user sys rss ops ok <<< "$$result"; \
			printf "%-8s %-21s %9s %9s %9s %10s %14s %6s\n" \
				"$$label" "$$name" "$$wall" "$$user" "$$sys" "$$rss" "$$ops" "$$ok"; \
		done; \
		echo ""; \
	done

run-%: $(OUT_DIR)/bench_%
	@$(RUN_STAGED) $< $(SCENARIO_ARGS)

list:
	@printf '%s\n' $(QUEUES)

$(TEST_BIN): $(TEST_SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(TEST_LDLIBS)

test: $(TEST_BIN)
	@$(RUN_STAGED) $<

$(TSAN_TEST_BIN): $(TSAN_TEST_SRC) | $(OUT_DIR)
	$(CXX) $(CPPFLAGS) $(TSAN_CXXFLAGS) -o $@ $< $(LDFLAGS) $(TSAN_LDLIBS)

test-tsan: $(TSAN_TEST_BIN)
	@$(SAN_CLEAN_ENV) TSAN_OPTIONS="halt_on_error=1" $(RUN_STAGED) $<

test-tsan-bench: $(TSAN_BENCH_BINS)
	@failed=0; \
	for q in $(QUEUES); do \
		echo "== TSAN bench: $$q =="; \
		if $(SAN_CLEAN_ENV) TSAN_OPTIONS="halt_on_error=1" $(RUN_STAGED) ./$(OUT_DIR)/bench_tsan_$$q $(TSAN_BENCH_ARGS); then \
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

test-tsan-all:
	$(MAKE) test-tsan
	$(MAKE) test-tsan-bench

test-asan-bench: $(ASAN_BENCH_BINS)
	@failed=0; \
	for q in $(QUEUES); do \
		echo "== ASAN bench: $$q =="; \
		if $(SAN_CLEAN_ENV) \
		   LD_PRELOAD="$(ASAN_RUNTIME_LIB)" \
		   ASAN_OPTIONS="$(ASAN_OPTIONS_BENCH)" \
		   $(RUN_STAGED) ./$(OUT_DIR)/bench_asan_$$q $(ASAN_BENCH_ARGS); then \
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
		if $(SAN_CLEAN_ENV) UBSAN_OPTIONS="halt_on_error=1" $(RUN_STAGED) ./$(OUT_DIR)/bench_ubsan_$$q $(UBSAN_BENCH_ARGS); then \
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

test-sanitize-all:
	$(MAKE) test-tsan-all
	$(MAKE) test-asan-bench
	$(MAKE) test-ubsan-bench

clean:
	@for attempt in 1 2 3 4 5; do \
		rm -rf $(OUT_DIR); \
		if [ ! -e $(OUT_DIR) ]; then \
			exit 0; \
		fi; \
		sleep 1; \
	done; \
	echo "Failed to remove $(OUT_DIR) after multiple attempts."; \
	exit 1

-include $(DEPS)
