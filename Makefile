CXX = g++
CXXFLAGS = -std=c++11 -Wall -g

TARGET = test
SRC = test.cpp
HEADERS = lock_free_list.h

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean