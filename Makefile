CC = gcc
CFLAGS = -Wall -Wextra -I./include -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INSTALL_DIR = /usr/local/bin

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
	@sudo install -m 755 $(TARGET) $(INSTALL_DIR)/$(BINARY)
	@echo "Installation complete! You can now run '$(BINARY)' from anywhere."

uninstall: clean
	@echo "Removing $(BINARY) from $(INSTALL_DIR)"
	@sudo rm -f $(INSTALL_DIR)/$(BINARY)
	@echo "Uninstall complete!"

clean:
	@rm -rf $(OBJ_DIR)
	@rm -rf $(BIN_DIR)