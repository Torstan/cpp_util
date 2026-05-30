CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
SRC_DIR := src
TEST_DIR := tests

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_BINS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
HEADERS := $(wildcard include/conn_util/*.h)
LINT_CPP_SRCS := $(SRCS) $(TEST_SRCS)
LINT_DIRS := include src tests
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.PHONY: all test clean lint

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS) $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(OBJS) -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
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

-include $(DEPS)
