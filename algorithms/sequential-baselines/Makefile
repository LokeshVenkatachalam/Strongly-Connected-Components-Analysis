CXX ?= g++
AR ?= ar

CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic
ARFLAGS := rcs

BUILD_DIR := build

LIB_SOURCES := \
	src/graph.cpp \
	src/io.cpp \
	src/registry.cpp \
	src/tarjan.cpp \
	src/gabow.cpp \
	src/pearce.cpp \
	src/tarjan_zwick.cpp
LIB_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(LIB_SOURCES))

COMPARE_OBJECT := $(BUILD_DIR)/src/scc_compare_main.o
BENCHMARK_OBJECT := $(BUILD_DIR)/src/scc_benchmark_main.o
TEST_OBJECT := $(BUILD_DIR)/tests/self_test.o
DEPENDENCIES := \
	$(LIB_OBJECTS:.o=.d) \
	$(COMPARE_OBJECT:.o=.d) \
	$(BENCHMARK_OBJECT:.o=.d) \
	$(TEST_OBJECT:.o=.d)

.PHONY: all check clean

all: scc_compare scc_benchmark exact_scc

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/libscc.a: $(LIB_OBJECTS)
	$(AR) $(ARFLAGS) $@ $^

scc_compare: $(COMPARE_OBJECT) $(BUILD_DIR)/libscc.a
	$(CXX) $(CXXFLAGS) $^ -o $@

scc_benchmark: $(BENCHMARK_OBJECT) $(BUILD_DIR)/libscc.a
	$(CXX) $(CXXFLAGS) $^ -o $@

scc_self_test: $(TEST_OBJECT) $(BUILD_DIR)/libscc.a
	$(CXX) $(CXXFLAGS) $^ -o $@

exact_scc: exact_scc.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

check: all scc_self_test
	./scc_self_test
	./exact_scc --arithmetic-test
	python3 tests/test_bgr_cli.py \
		./scc_compare ./scc_benchmark ./exact_scc

clean:
	rm -rf $(BUILD_DIR)
	rm -f scc_compare scc_benchmark scc_self_test exact_scc

-include $(DEPENDENCIES)
