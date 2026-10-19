# Makefile for the C32 CPU emulator.
#
#   make              build everything into ./build
#   make test         run unit tests + every example program
#   make examples     assemble every example into build/examples/*.c32
#   make run PROG=examples/factorial.asm [ARGS="--stats"]
#   make isa          print the instruction-set reference
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow
CPPFLAGS += -Iinclude
BUILD    := build

LIB_SRCS := src/isa.cpp src/memory.cpp src/cpu.cpp src/assembler.cpp src/program.cpp src/disassembler.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(BUILD)/obj/%.o)
LIB      := $(BUILD)/libc32.a
TOOLS    := $(BUILD)/c32asm $(BUILD)/c32sim $(BUILD)/c32dis
TESTS    := $(BUILD)/c32tests
DEPS     := $(LIB_OBJS:.o=.d) $(BUILD)/obj/tools/c32asm.d $(BUILD)/obj/tools/c32sim.d \
            $(BUILD)/obj/tools/c32dis.d $(BUILD)/obj/tests/test_main.d

EXAMPLES := $(wildcard examples/*.asm)

.PHONY: all test unit-test example-test examples run isa clean

all: $(TOOLS) $(TESTS)

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/tools/%.o: tools/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/c32%: $(BUILD)/obj/tools/c32%.o $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

$(TESTS): $(BUILD)/obj/tests/test_main.o $(LIB)
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

test: unit-test example-test

unit-test: $(TESTS)
	./$(TESTS)

example-test: $(TOOLS)
	./scripts/run_examples.sh

examples: $(BUILD)/c32asm
	@mkdir -p $(BUILD)/examples
	@for f in $(EXAMPLES); do ./$(BUILD)/c32asm $$f -o $(BUILD)/examples/$$(basename $${f%.asm}).c32 || exit 1; done

run: $(BUILD)/c32sim
	@test -n "$(PROG)" || (echo "usage: make run PROG=examples/factorial.asm [ARGS=--stats]"; exit 1)
	./$(BUILD)/c32sim $(ARGS) $(PROG)

isa: $(BUILD)/c32dis
	./$(BUILD)/c32dis --isa

clean:
	rm -rf $(BUILD)

-include $(DEPS)
