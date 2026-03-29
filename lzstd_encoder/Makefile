CXX = g++
CXXFLAGS = -O3 -Wall -std=c++17
LDFLAGS = -lzstd -llzma -lpthread

TARGET = lzstd_encoder

all: $(TARGET)

$(TARGET): lzstd_encoder.cc
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
