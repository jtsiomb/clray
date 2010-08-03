src = $(wildcard src/*.cc)
obj = $(src:.cc=.o)
dep = $(obj:.o=.d)
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

-include $(dep)

%.d: %.cc
	@$(CPP) $(CXXFLAGS) -MM -MT $(@:.d=.o) $< >$@

.PHONY: clean
clean:
	rm -f $(obj) $(bin) $(dep)
