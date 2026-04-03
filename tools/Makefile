CXX      ?= clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRCDIR   := ../src
INCLUDES := -I$(SRCDIR)

# Shared firmware sources (no Arduino dependency)
SHARED_SRCS := \
	$(SRCDIR)/core/layout_engine.cpp \
	$(SRCDIR)/core/vertical_layout_engine.cpp \
	$(SRCDIR)/display/layout_engine.cpp \
	$(SRCDIR)/display/badge_renderer.cpp \
	$(SRCDIR)/transit/mta_color_map.cpp

.PHONY: all preview clean

all: preview

preview: led_preview
led_preview: led_preview.cpp $(SHARED_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

clean:
	rm -f led_preview
