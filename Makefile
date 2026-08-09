CC ?= gcc
OPT ?= -O2

# gcc takes -fopenmp direct, apple clang needs brew libomp, none means the pragmas compile away
OMP := $(shell echo 'int main(void){return 0;}' | $(CC) -fopenmp -x c - -o /dev/null 2>/dev/null && echo native || { [ -f /opt/homebrew/opt/libomp/include/omp.h ] && echo brew || echo none; })
OMP_CFLAGS  = $(if $(filter brew,$(OMP)),-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include,$(if $(filter native,$(OMP)),-fopenmp))
OMP_LDFLAGS = $(if $(filter brew,$(OMP)),-L/opt/homebrew/opt/libomp/lib -lomp,$(if $(filter native,$(OMP)),-fopenmp))

CFLAGS = -std=c11 -Wall -Wextra -I./include $(OPT) -MMD -MP -pthread -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_DARWIN_C_SOURCE $(OMP_CFLAGS)
LDFLAGS = -pthread $(OMP_LDFLAGS)

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INSTALL_DIR ?= /usr/local/bin

BINARY = notebetter
TARGET = $(BIN_DIR)/$(BINARY)

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean directories install uninstall config

all: directories $(TARGET)

config:
	@echo "CC=$(CC)  OPT=$(OPT)  OpenMP=$(OMP)"

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | directories
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	@echo "Installing $(BINARY) to $(INSTALL_DIR)"
	@install -m 755 $(TARGET) $(INSTALL_DIR)/$(BINARY)
	@echo "Installation complete! You can now run '$(BINARY)' from anywhere."

uninstall:
	@echo "Removing $(BINARY) from $(INSTALL_DIR)"
	@rm -f $(INSTALL_DIR)/$(BINARY)
	@echo "Uninstall complete!"

clean:
	@rm -rf $(OBJ_DIR)
	@rm -rf $(BIN_DIR)

-include $(OBJS:.o=.d)
