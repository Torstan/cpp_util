CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_DIR := tests

TEST_BINS := \
	$(BUILD_DIR)/common_test \
	$(BUILD_DIR)/point_circle_test \
	$(BUILD_DIR)/edge_polygon_test \
	$(BUILD_DIR)/range_tree_test \
	$(BUILD_DIR)/kdtree_test

TEST_UTILS := $(TEST_DIR)/test_utils.cpp $(TEST_DIR)/test_utils.h

.PHONY: all test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/common_test: $(TEST_DIR)/common_test.cpp $(TEST_UTILS) include/computational_geometry/common.h include/computational_geometry/edge_polygon.h include/computational_geometry/point_circle.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/common_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/point_circle_test: $(TEST_DIR)/point_circle_test.cpp $(TEST_UTILS) include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/point_circle_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/edge_polygon_test: $(TEST_DIR)/edge_polygon_test.cpp $(TEST_UTILS) include/computational_geometry/edge_polygon.h include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/edge_polygon_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/range_tree_test: $(TEST_DIR)/range_tree_test.cpp $(TEST_UTILS) include/computational_geometry/range_tree.h include/computational_geometry/edge_polygon.h include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/range_tree_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/kdtree_test: $(TEST_DIR)/kdtree_test.cpp include/computational_geometry/kdtree.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/kdtree_test.cpp -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

clean:
	rm -rf $(BUILD_DIR)
