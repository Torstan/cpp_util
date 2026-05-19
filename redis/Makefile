CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_DIR := tests
BENCH_DIR := benchmarks
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_BINS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
BENCH_SRCS := $(wildcard $(BENCH_DIR)/*.cpp)
BENCH_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%,$(BENCH_SRCS))
HEADERS := $(wildcard include/redis/*.h)
LINT_CPP_SRCS := $(TEST_SRCS) $(BENCH_SRCS)
LINT_DIRS := include $(TEST_DIR) $(BENCH_DIR)
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.PHONY: all test bench lint clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(TEST_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/bench_%: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

bench: $(BENCH_BINS)
	@for bench_bin in $(BENCH_BINS); do \
		./$$bench_bin; \
	done

lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
		$(CLANG_TIDY) $(CLANG_TIDY_FLAGS) $(LINT_CPP_SRCS) -- $(CPPFLAGS) $(CXXFLAGS); \
	else \
		echo "clang-tidy not installed; skipping"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		$(CPPCHECK) $(CPPCHECK_FLAGS) $(LINT_DIRS); \
	else \
		echo "cppcheck not installed; skipping"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
