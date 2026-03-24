CXX      ?= clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRCDIR   := ../src
INCLUDES := -I$(SRCDIR)

# SDL2
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# Mosquitto (MQTT client library)
MOSQ_CFLAGS := $(shell pkg-config --cflags libmosquitto 2>/dev/null || echo "-I/opt/homebrew/include")
MOSQ_LIBS   := $(shell pkg-config --libs libmosquitto 2>/dev/null || echo "-L/opt/homebrew/lib -lmosquitto")

# Shared firmware sources (no Arduino dependency)
SHARED_SRCS := \
	$(SRCDIR)/core/layout_engine.cpp \
	$(SRCDIR)/core/vertical_layout_engine.cpp \
	$(SRCDIR)/display/layout_engine.cpp \
	$(SRCDIR)/display/badge_renderer.cpp \
	$(SRCDIR)/transit/mta_color_map.cpp

# Emscripten (WebAssembly build)
EMCXX    ?= em++
EM_FLAGS := -std=c++17 -O2 -s WASM=1 \
            -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","lengthBytesUTF8","stringToUTF8"]' \
            -s EXPORTED_FUNCTIONS='["_malloc","_free","_wasm_init","_wasm_get_width","_wasm_get_height","_wasm_get_pixels","_wasm_apply_payload","_wasm_set_display_type"]' \
            -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME="SimModule"

.PHONY: all simulator simulator-mqtt preview web clean

all: simulator-mqtt preview

# Basic simulator (mock scenarios only, no MQTT dependency)
simulator: led_simulator
led_simulator: led_simulator.cpp $(SHARED_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS)

# Full simulator with MQTT live mode
simulator-mqtt: led_simulator_mqtt
led_simulator_mqtt: led_simulator.cpp $(SHARED_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL_CFLAGS) $(MOSQ_CFLAGS) -DHAS_MOSQUITTO -o led_simulator $^ $(SDL_LIBS) $(MOSQ_LIBS)

preview: led_preview
led_preview: led_preview.cpp $(SHARED_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# WebAssembly simulator (requires Emscripten SDK)
web: web/bridge.js

web/bridge.js: web/bridge.cpp $(SHARED_SRCS)
	$(EMCXX) $(EM_FLAGS) $(INCLUDES) -o $@ $^

clean:
	rm -f led_simulator led_simulator_mqtt led_preview web/bridge.js web/bridge.wasm
