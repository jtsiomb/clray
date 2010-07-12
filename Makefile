src = $(wildcard src/*.cc)
obj = $(src:.cc=.o)
bin = test

CXX = g++
CXXFLAGS = -pedantic -Wall -g
LDFLAGS = -framework OpenCL

$(bin): $(obj)
	$(CXX) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
