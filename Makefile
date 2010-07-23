src = $(wildcard src/*.cc)
obj = $(src:.cc=.o)
bin = test

CXX = g++
CXXFLAGS = -pedantic -Wall -g
LDFLAGS = $(libgl) $(libcl)

ifeq ($(shell uname -s), Darwin)
	libgl = -framework OpenGL -framework GLUT
	libcl = -framework OpenCL
else
	libgl = -lGL -lglut
	libcl = -lOpenCL
endif

$(bin): $(obj)
	$(CXX) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
