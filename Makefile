CXX ?= c++
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
SOURCES := src/main.cpp src/engine.cpp src/render.cpp src/platform.cpp
ifeq ($(shell uname -s),Linux)
PLATFORM_LIBS := -ldl
endif

uskok: $(SOURCES) src/engine.hpp
	$(CXX) -std=c++20 $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) $(PLATFORM_LIBS) -o $@

.PHONY: clean
clean:
	rm -f uskok
