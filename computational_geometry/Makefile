CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := common_test point_circle_test edge_polygon_test range_tree_test kdtree_test
TEST_SOURCES := test_utils.cpp

.PHONY: all test clean

all: $(TARGETS)

common_test: common_test.cpp $(TEST_SOURCES) test_utils.h common.h point_circle.h edge_polygon.h
	$(CXX) $(CXXFLAGS) common_test.cpp $(TEST_SOURCES) -o $@

point_circle_test: point_circle_test.cpp $(TEST_SOURCES) test_utils.h point_circle.h common.h
	$(CXX) $(CXXFLAGS) point_circle_test.cpp $(TEST_SOURCES) -o $@

edge_polygon_test: edge_polygon_test.cpp $(TEST_SOURCES) test_utils.h edge_polygon.h point_circle.h common.h
	$(CXX) $(CXXFLAGS) edge_polygon_test.cpp $(TEST_SOURCES) -o $@

range_tree_test: range_tree_test.cpp $(TEST_SOURCES) test_utils.h range_tree.h edge_polygon.h point_circle.h common.h
	$(CXX) $(CXXFLAGS) range_tree_test.cpp $(TEST_SOURCES) -o $@

kdtree_test: kdtree_test.cpp kdtree.cpp kdtree.h
	$(CXX) $(CXXFLAGS) kdtree_test.cpp kdtree.cpp -o $@

test: all
	./common_test
	./point_circle_test
	./edge_polygon_test
	./range_tree_test
	./kdtree_test

clean:
	rm -f $(TARGETS)
