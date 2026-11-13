# invar — property-based testing for C
#
# Layout:
#   src/    sources and headers
#   build/  object files and dependency files
#   bin/    linked executables
#
# Targets:
#   make            normal build        -> build/, bin/
#   make san        ASan + UBSan build  -> build/san/, bin/san/
#   make run        build and run the example
#   make run-san    build and run the example under sanitizers
#   make clean      remove build/ and bin/

CC      ?= cc
CSTD    := -std=gnu23
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
CFLAGS  ?= -O2 -g
CPPFLAGS += -Isrc -MMD -MP
LDFLAGS ?=
LDLIBS  ?=

SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all \
             -fno-omit-frame-pointer

# SAN=1 switches every output into its own subdirectory, so sanitized and
# plain objects never get mixed up and you never need a clean between them.
ifeq ($(SAN),1)
  BUILD   := build/san
  BIN     := bin/san
  CFLAGS  := -O1 -g $(SAN_FLAGS)
  LDFLAGS += $(SAN_FLAGS)
else
  BUILD   := build
  BIN     := bin
endif

LIB_SRC := src/invar.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)

EXAMPLE := $(BIN)/example

all: $(EXAMPLE)

$(EXAMPLE): $(BUILD)/example.o $(LIB_OBJ) | $(BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD) $(BIN):
	mkdir -p $@

run: $(EXAMPLE)
	- ./$(EXAMPLE)

# Re-invoke make with SAN=1 so the variable applies to the whole build.
san:
	$(MAKE) SAN=1 all

# By default a sanitizer report ends the process with exit(1), which invar
# would show as "exited early with code 1". abort_on_error makes it raise
# SIGABRT instead, so the failure is reported as a signal like any other crash.
# The full sanitizer report still goes to stderr.
SAN_ENV := ASAN_OPTIONS=abort_on_error=1 \
           UBSAN_OPTIONS=abort_on_error=1:print_stacktrace=1

run-san:
	$(MAKE) SAN=1 all
	- $(SAN_ENV) ./bin/san/example

clean:
	rm -rf build bin

-include $(wildcard $(BUILD)/*.d)

.PHONY: all run san run-san clean
