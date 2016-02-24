CXXFLAGS := -Wall -g -std=c++11 -I.
LDFLAGS := -pthread
LIBS := -lboost_unit_test_framework

HDRS := $(wildcard *.h)
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,%.o,$(SRCS))

TARGET := idgentest

all: Makefile $(TARGET)

$(TARGET): $(HDRS) $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -rf *.o $(TARGET)

.PHONY: all clean
