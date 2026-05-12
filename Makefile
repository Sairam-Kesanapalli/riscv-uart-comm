# =============================================================================
# Makefile for RISC-V ACT UART Communication Program
# =============================================================================
# Build:   make
# Clean:   make clean
# Run:     make run DEVICE=/dev/pts/X BAUD=9600
# =============================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=gnu11 -O2
TARGET   = uart_comm
SRC      = uart_comm.c

# Default UART parameters (override on command line)
DEVICE   ?= /dev/ttyUSB0
BAUD     ?= 9600

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "[BUILD] $(TARGET) built successfully"

clean:
	rm -f $(TARGET)
	@echo "[CLEAN] Removed $(TARGET)"

run: $(TARGET)
	./$(TARGET) $(DEVICE) $(BAUD)

help:
	@echo "Targets:"
	@echo "  all   - Build the program (default)"
	@echo "  clean - Remove the binary"
	@echo "  run   - Build and run (use DEVICE= and BAUD= to configure)"
	@echo "  help  - Show this message"
	@echo ""
	@echo "Example:"
	@echo "  make run DEVICE=/dev/pts/3 BAUD=115200"
