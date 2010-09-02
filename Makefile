src = $(wildcard src/*.cc)
obj = $(src:.cc=.o)
dep = $(obj:.o=.d)
bin = clray

opt = -O3 -ffast-math
dbg = -g

CXX = g++
CXXFLAGS = -pedantic -Wall $(dbg) $(opt) $(def)
LDFLAGS = $(libgl) $(libcl) -lpthread

ifeq ($(shell uname -s), Darwin)
	libgl = -framework OpenGL -framework GLUT
	libcl = -framework OpenCL
else
	libgl = -lGL -lGLU -lglut
	libcl = -lOpenCL
	def = -DCLGL_INTEROP
endif

$(bin): $(obj)
	$(CXX) -o $@ $(obj) $(LDFLAGS)

-include $(dep)

%.d: %.cc
	@$(CPP) $(CXXFLAGS) -MM -MT $(@:.d=.o) $< >$@

.PHONY: clean
clean:
	rm -f $(obj) $(bin) $(dep)
