CXX = g++
CXXFLAGS = -std=c++11 -Wall -g

TARGETS = lock_free_test test_coarse_grain
LOCK_FREE_SRC = test.cpp
COARSE_GRAIN_SRC = test_coarse_grain.cpp
HEADERS = lock_free_list.h coarse_grain_list.h

all: $(TARGETS)

lock_free_test: $(LOCK_FREE_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(LOCK_FREE_SRC)

coarse_grain_test: $(COARSE_GRAIN_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(COARSE_GRAIN_SRC)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
