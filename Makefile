CC := gcc
CFLAGS := -c -Wall
BINARY := bin
SRC_DIR := src
BUILD_DEBUG := buildDebug
BUILD_RELEASE := buildRelease
DEBUG_OPTIONS := -DDEBUG_LOG_GC -DDEBUG_PRINT_CODE
DEBUG_TARGET := bin/clox-debug
RELEASE_TARGET := bin/clox

SRC_C := $(foreach dir, $(SRC_DIR), $(wildcard $(dir)/*.c))
DEBUG_OBJ_C := $(addprefix $(BUILD_DEBUG)/,$(patsubst %.c,%.o,$(notdir $(SRC_C))))
RELEASE_OBJ_C := $(addprefix $(BUILD_RELEASE)/,$(patsubst %.c,%.o,$(notdir $(SRC_C))))

ifeq ($(shell arch), x86_64)
	DEBUG_OPTIONS+= -DNAN_BOXING
endif
$(RELEASE_TARGET): $(RELEASE_OBJ_C)
	$(CC) -o $@ $^

$(DEBUG_TARGET): $(DEBUG_OBJ_C)
	$(CC) -o $@ $^

$(BUILD_DEBUG)/%.o: $(SRC_DIR)/%.c
	$(CC) $(DEBUG_OPTIONS) $(CFLAGS) $< -o $@

$(BUILD_RELEASE)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: all clean CHECK_FOLDER test

all: CHECK_FOLDER $(DEBUG_TARGET) $(RELEASE_TARGET)

CHECK_FOLDER:
	@if [ ! -d $(BUILD_DEBUG) ]; then mkdir -p $(BUILD_DEBUG); fi
	@if [ ! -d $(BUILD_RELEASE) ]; then mkdir -p $(BUILD_RELEASE); fi
	@if [ ! -d $(BINARY) ]; then mkdir -p $(BINARY); fi

clean:
	rm -rf $(BUILD_DEBUG)/* $(BUILD_RELEASE)/* $(BINARY)/*

test:
	sh test.sh
