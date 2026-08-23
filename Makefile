OPT ?= -O2
BUILD ?= release
INSTALL_DIR ?= /usr/local/bin
CORPUS_LINES ?= 1000000

# gcc takes -fopenmp direct, apple clang needs brew libomp, none means the pragmas compile away
OMP := $(shell echo 'int main(void){return 0;}' | $(CC) -fopenmp -x c - -o /dev/null 2>/dev/null && echo native || { [ -f /opt/homebrew/opt/libomp/include/omp.h ] && echo brew || echo none; })
OMP_CFLAGS  = $(if $(filter brew,$(OMP)),-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include,$(if $(filter native,$(OMP)),-fopenmp))
OMP_LDFLAGS = $(if $(filter brew,$(OMP)),-L/opt/homebrew/opt/libomp/lib -lomp,$(if $(filter native,$(OMP)),-fopenmp))

CFLAGS  = -std=c11 -Wall -Wextra -I./include $(OPT) -MMD -MP -pthread -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_DARWIN_C_SOURCE $(OMP_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS = -pthread $(OMP_LDFLAGS) $(EXTRA_LDFLAGS)

OBJ_DIR = obj/$(BUILD)
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(wildcard src/*.c))
LIB_OBJS = $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
CORPUS = bin/corpus$(CORPUS_LINES).c

# non release builds get a suffix, else `make asan` clobbers bin/bench and the next `make bench` measures an instrumented build
S = $(if $(filter release,$(BUILD)),,-$(BUILD))

.PHONY: all clean install uninstall config test bench tsan asan

all: bin/notebetter

config:
	@echo "CC=$(CC)  OPT=$(OPT)  OpenMP=$(OMP)  BUILD=$(BUILD)"

bin/notebetter: $(OBJS)
	@mkdir -p bin
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# test and bench bring their own main, so they link everything but main.o
bin/test$(S): tests/test.c $(LIB_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(LIB_OBJS) $(LDFLAGS) -o $@

bin/bench$(S): bench/bench.c $(LIB_OBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(LIB_OBJS) $(LDFLAGS) -o $@

test: bin/test$(S)
	@bin/test$(S)

bench: bin/bench$(S)
	@[ -f $(CORPUS) ] || bin/bench$(S) --gen $(CORPUS_LINES) $(CORPUS)
	@bin/bench$(S) $(CORPUS) 200

# cc because gcc has no working sanitizers on arm64 macos; OMP=none because libomp is uninstrumented, so tsan cannot see its barriers and calls every shared read either side of one a race
tsan:
	@$(MAKE) CC=cc BUILD=tsan OMP=none CORPUS_LINES=200000 EXTRA_CFLAGS="-fsanitize=thread -g -O1" EXTRA_LDFLAGS="-fsanitize=thread" test bench

asan:
	@$(MAKE) CC=cc BUILD=asan CORPUS_LINES=200000 EXTRA_CFLAGS="-fsanitize=address,undefined -g -O1" EXTRA_LDFLAGS="-fsanitize=address,undefined" test bench

install: all
	install -m 755 bin/notebetter $(INSTALL_DIR)/notebetter

uninstall:
	rm -f $(INSTALL_DIR)/notebetter

clean:
	rm -rf obj bin

-include $(OBJS:.o=.d)
