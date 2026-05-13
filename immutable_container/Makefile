CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS

BUILD_DIR := build
TEST_DIR := tests
BENCH_DIR := benchmarks
REPORT_DIR := reports
STRING_REPORT_BENCH_SRC := $(BENCH_DIR)/block_tree_string_report_bench.cpp
STRING_REPORT_BENCH_BIN := $(BUILD_DIR)/bench_block_tree_string_report_jemalloc
STRING_REPORT_RAW := $(BUILD_DIR)/immutable_tree_vs_block_tree_string.csv
STRING_REPORT_HTML := $(REPORT_DIR)/immutable_tree_vs_block_tree.html
JEMALLOC_DIR ?= ../thirdparty/jemalloc
JEMALLOC_OBJ_DIR ?= $(JEMALLOC_DIR)/obj
JEMALLOC_INCLUDE := $(JEMALLOC_OBJ_DIR)/include
JEMALLOC_STATIC_LIB := $(JEMALLOC_OBJ_DIR)/lib/libjemalloc.a
JEMALLOC_LOCAL_LIBS ?= -Wl,--whole-archive $(JEMALLOC_STATIC_LIB) -Wl,--no-whole-archive -pthread -ldl -lm
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_BINS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
BENCH_SRCS := $(filter-out $(STRING_REPORT_BENCH_SRC),$(wildcard $(BENCH_DIR)/*.cpp))
BENCH_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%,$(BENCH_SRCS))
JEMALLOC_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%_jemalloc,$(BENCH_SRCS))
JEMALLOC_LIBS ?= -ljemalloc
HEADERS := $(wildcard include/immutable_container/*.h)
LINT_CPP_SRCS := $(TEST_SRCS)
LINT_DIRS := include $(TEST_DIR)
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.DELETE_ON_ERROR:

.PHONY: all test bench bench-jemalloc string-report clean lint

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(REPORT_DIR):
	mkdir -p $(REPORT_DIR)

$(BUILD_DIR)/%: $(TEST_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/bench_%: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/bench_%_jemalloc: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -DIMMUTABLE_CONTAINER_USE_JEMALLOC $(CXXFLAGS) $< -o $@ $(JEMALLOC_LIBS)

$(JEMALLOC_STATIC_LIB):
	@if [ ! -x "$(JEMALLOC_DIR)/autogen.sh" ]; then \
		echo "missing $(JEMALLOC_DIR)/autogen.sh"; \
		exit 1; \
	fi
	@if [ ! -x "$(JEMALLOC_DIR)/configure" ]; then \
		cd "$(JEMALLOC_DIR)" && ./autogen.sh; \
	fi
	mkdir -p $(JEMALLOC_OBJ_DIR)
	@if [ ! -f "$(JEMALLOC_OBJ_DIR)/Makefile" ]; then \
		cd "$(JEMALLOC_OBJ_DIR)" && ../configure --enable-autogen; \
	fi
	$(MAKE) -C $(JEMALLOC_OBJ_DIR) lib/libjemalloc.a

$(STRING_REPORT_BENCH_BIN): $(STRING_REPORT_BENCH_SRC) $(HEADERS) Makefile $(JEMALLOC_STATIC_LIB) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -I$(JEMALLOC_INCLUDE) -DIMMUTABLE_CONTAINER_USE_JEMALLOC $(CXXFLAGS) $< -o $@ $(JEMALLOC_LOCAL_LIBS)

$(STRING_REPORT_RAW): $(STRING_REPORT_BENCH_BIN)
	@tmp="$(STRING_REPORT_RAW).tmp"; \
	rm -f "$$tmp"; \
	./$(STRING_REPORT_BENCH_BIN) --env > "$$tmp"; \
	for name in immutable_tree block_tree_2048 block_tree_4096; do \
		for pattern in sorted random; do \
			for key_bytes in 32 64; do \
				for value_bytes in 64 128 256 1024; do \
					for size in 1 10 100 1000 10000 100000; do \
						./$(STRING_REPORT_BENCH_BIN) --name "$$name" --pattern "$$pattern" \
							--size "$$size" --key-bytes "$$key_bytes" \
							--value-bytes "$$value_bytes" >> "$$tmp" || exit $$?; \
					done; \
				done; \
			done; \
		done; \
	done; \
	mv "$$tmp" "$(STRING_REPORT_RAW)"

$(STRING_REPORT_HTML): $(STRING_REPORT_RAW) scripts/generate_block_tree_report.py Makefile | $(REPORT_DIR)
	python3 scripts/generate_block_tree_report.py --input $(STRING_REPORT_RAW) --output $(STRING_REPORT_HTML) --bench-command ./$(STRING_REPORT_BENCH_BIN) --jemalloc-path $(JEMALLOC_DIR)

string-report: $(STRING_REPORT_HTML)

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

bench: $(BENCH_BINS)
	@for bench_bin in $(BENCH_BINS); do \
		./$$bench_bin; \
	done

bench-jemalloc: $(JEMALLOC_BINS)
	@for bench_bin in $(JEMALLOC_BINS); do \
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
