src = $(wildcard src/*.cc)
obj = $(src:.cc=.o)
bin = test

CXX = g++
CXXFLAGS = -pedantic -Wall -g
LDFLAGS = -framework OpenGL -framework GLUT -framework OpenCL

$(bin): $(obj)
	$(CXX) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
