# Makefile for Raylib project on Linux

# Compiler
CC = gcc

# Project name
PROJECT_NAME ?= ccanvas

# Source files
SRCS = $(wildcard src/*.c)

# Build directory
BUILD_DIR ?= build
OBJ_DIR = $(BUILD_DIR)/obj

# Object files
OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Executable name
TARGET = $(BUILD_DIR)/$(PROJECT_NAME)

# Compiler and linker flags
# Use pkg-config to get the correct flags for raylib
CFLAGS ?= -Wall -Wextra -std=c99 -g `pkg-config --cflags raylib` -Isrc
LDFLAGS ?= `pkg-config --libs raylib` -lm

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build finished: $(TARGET)"

# Compile source files to object files
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned build files."

# Run the application
run: all
	./$(TARGET)

.PHONY: all clean run
