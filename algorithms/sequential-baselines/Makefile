CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all check clean

all: scc_compare exact_scc

scc_compare: scc_compare.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

exact_scc: exact_scc.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

check: all
	./scc_compare --self-test
	./exact_scc --arithmetic-test
	python3 tests/test_bgr_cli.py ./scc_compare ./exact_scc

clean:
	rm -f scc_compare exact_scc
