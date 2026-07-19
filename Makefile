CC ?= gcc
OPT ?= -O2
CFLAGS = -std=c11 -Wall -Wextra -I./include $(OPT) -MMD -MP -pthread -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_DARWIN_C_SOURCE
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INSTALL_DIR ?= /usr/local/bin

BINARY = notebetter
TARGET = $(BIN_DIR)/$(BINARY)

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean directories install uninstall

all: directories $(TARGET)

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
