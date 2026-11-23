TARGET = Karma
APP_TYPE = BOOT_SRAM
CPP_SOURCES = main.cpp
C_SOURCES += $(AURORA_SDK_DIR)/libs/libDaisy/Middlewares/Third_Party/FatFs/src/option/ccsbcs.c

AURORA_SDK_DIR ?= ../../lib/Aurora-SDK
C_INCLUDES += -I$(AURORA_SDK_DIR)/include/
C_INCLUDES += -I$(abspath ../shared/include)

LIBDAISY_DIR = $(AURORA_SDK_DIR)/libs/libDaisy/
DAISYSP_DIR = $(AURORA_SDK_DIR)/libs/DaisySP/
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

include $(SYSTEM_FILES_DIR)/Makefile
