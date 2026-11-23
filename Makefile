JUCE_SOURCE_DIR ?= ../lib/JUCE-9.0.4-osx
AURORA_SDK_DIR ?= lib/Aurora-SDK
GCC_PATH ?= /Library/DaisyToolchain/0.1.2/arm/bin
BUILD_DIR ?= build
CONFIG ?= Release

.PHONY: all vst aurora libdaisy test clean

all: aurora

vst:
	cmake -S . -B "$(BUILD_DIR)/vst" -DJUCE_SOURCE_DIR="$(abspath $(JUCE_SOURCE_DIR))"
	cmake --build "$(BUILD_DIR)/vst" --config "$(CONFIG)" --target KarmaTestBed_VST3

libdaisy:
	@test -f "$(AURORA_SDK_DIR)/include/aurora.h" || { echo "Aurora SDK not found. Set AURORA_SDK_DIR to its checkout."; exit 1; }
	$(MAKE) -C "$(AURORA_SDK_DIR)/libs/libDaisy" GCC_PATH="$(GCC_PATH)"
	$(MAKE) -C "$(AURORA_SDK_DIR)/libs/DaisySP" GCC_PATH="$(GCC_PATH)"

aurora: libdaisy
	$(MAKE) -C firmware AURORA_SDK_DIR="$(abspath $(AURORA_SDK_DIR))" GCC_PATH="$(GCC_PATH)" BUILD_DIR="build"

test:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Ishared/include tests/StereoLooperTests.cpp -o "$(BUILD_DIR)/tests/stereo-looper-tests"
	"$(BUILD_DIR)/tests/stereo-looper-tests"

clean:
	cmake -E rm -rf build
	$(MAKE) -C firmware clean
