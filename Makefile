CC = gcc
CFLAGS = -c -Wall
D_SRC = src
D_OBJ = buildDebug
R_OBJ = buildRelease
DEBUG_OPTIONS = -DDEBUG_LOG_GC -DDEBUG_PRINT_CODE
DEBUG_TARGET = clox-debug
RELEASE_TARGET = clox

SRC_C = $(foreach dir, $(D_SRC), $(wildcard $(dir)/*.c))
DEBUG_OBJ_C = $(addprefix $(D_OBJ)/,$(patsubst %.c,%.o,$(notdir $(SRC_C))))
RELEASE_OBJ_C = $(addprefix $(R_OBJ)/,$(patsubst %.c,%.o,$(notdir $(SRC_C))))

$(RELEASE_TARGET):$(RELEASE_OBJ_C)
	$(CC) -o $@ $^

$(DEBUG_TARGET):$(DEBUG_OBJ_C)
	$(CC) -o $@ $^

$(D_OBJ)/%.o:$(D_SRC)/%.c
	$(CC) $(DEBUG_OPTIONS) $(CFLAGS) $< -o $@

$(R_OBJ)/%.o:$(D_SRC)/%.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY: all clean createFloder
all: $(DEBUG_TARGET) $(RELEASE_TARGET) createFloder
createFloder:
	if [ ! -d $(D_OBJ) ]; then mkdir -p $(D_OBJ); fi
	if [ ! -d $(R_OBJ) ]; then mkdir -p $(R_OBJ); fi
clean:
	rm -f $(D_OBJ)/* $(R_OBJ)/* $(DEBUG_TARGET) $(RELEASE_TARGET)

