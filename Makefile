JUCE_SOURCE_DIR ?= ../lib/JUCE-9.0.4-osx
AURORA_SDK_DIR ?= lib/Aurora-SDK
GCC_PATH ?= /Library/DaisyToolchain/0.1.2/arm/bin
BUILD_DIR ?= build
CONFIG ?= Release
DIAGNOSTIC ?= 0

DIAGNOSTIC_BUILD_DIR := build
ifeq ($(DIAGNOSTIC),0)
DIAGNOSTIC_BUILD_DIR := build
else ifeq ($(DIAGNOSTIC),1)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-pass
else ifeq ($(DIAGNOSTIC),2)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-tone
else ifeq ($(DIAGNOSTIC),3)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-boost
else ifeq ($(DIAGNOSTIC),5)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-controls
else ifeq ($(DIAGNOSTIC),6)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-8x
else ifeq ($(DIAGNOSTIC),7)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-sdram
else ifeq ($(DIAGNOSTIC),8)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-controls-cv
else ifeq ($(DIAGNOSTIC),9)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-usb
else ifeq ($(DIAGNOSTIC),10)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-playback
else ifeq ($(DIAGNOSTIC),11)
DIAGNOSTIC_BUILD_DIR := build-diagnostic-record-audit
endif

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
	$(MAKE) -C firmware AURORA_SDK_DIR="$(abspath $(AURORA_SDK_DIR))" GCC_PATH="$(GCC_PATH)" DIAGNOSTIC="$(DIAGNOSTIC)" BUILD_DIR="$(DIAGNOSTIC_BUILD_DIR)"

test:
	mkdir -p "$(BUILD_DIR)/tests"
	$(CXX) -std=c++11 -Wall -Wextra -Werror -Ishared/include tests/StereoLooperTests.cpp -o "$(BUILD_DIR)/tests/stereo-looper-tests"
	"$(BUILD_DIR)/tests/stereo-looper-tests"

clean:
	cmake -E rm -rf build
	$(MAKE) -C firmware clean
