#!/bin/bash
# =============================================================================
# test_loopback.sh — Automated UART Loopback Test Using socat
# =============================================================================
#
# This script creates a virtual serial port pair using socat, runs the UART
# program on one end, sends test data from the other end, and verifies
# the communication works end-to-end — all without physical hardware.
#
# Prerequisites:
#   - socat:  sudo apt install socat
#   - gcc:    sudo apt install gcc (or build-essential)
#
# Usage:
#   chmod +x test_loopback.sh
#   ./test_loopback.sh
#
# =============================================================================

set -e

BAUD=9600
BINARY="./uart_comm"
SOCAT_PID=""
PTY_A=""
PTY_B=""

# Colors for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ---------------------------------------------------------------------------
# Cleanup function — ensures socat and child processes are killed on exit
# ---------------------------------------------------------------------------
cleanup() {
    echo -e "\n${YELLOW}[CLEANUP]${NC} Shutting down..."
    if [ -n "$SOCAT_PID" ] && kill -0 "$SOCAT_PID" 2>/dev/null; then
        kill "$SOCAT_PID" 2>/dev/null
        wait "$SOCAT_PID" 2>/dev/null || true
        echo -e "${YELLOW}[CLEANUP]${NC} socat (PID $SOCAT_PID) terminated"
    fi
    # Kill any background sender processes
    jobs -p | xargs -r kill 2>/dev/null || true
    echo -e "${YELLOW}[CLEANUP]${NC} Done"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
echo -e "${CYAN}==============================================================${NC}"
echo -e "${CYAN}  UART Loopback Test — Virtual Serial Port with socat${NC}"
echo -e "${CYAN}==============================================================${NC}"
echo ""

# Check that socat is installed
if ! command -v socat &>/dev/null; then
    echo -e "${RED}[ERROR]${NC} socat is not installed."
    echo "        Install it with: sudo apt install socat"
    exit 1
fi
echo -e "${GREEN}[OK]${NC} socat found: $(which socat)"

# Build the program if needed
if [ ! -f "$BINARY" ]; then
    echo -e "${YELLOW}[BUILD]${NC} Compiling uart_comm.c..."
    make
fi
echo -e "${GREEN}[OK]${NC} Binary ready: $BINARY"

# ---------------------------------------------------------------------------
# Step 1: Create a virtual serial port pair with socat
# ---------------------------------------------------------------------------
echo ""
echo -e "${CYAN}[STEP 1]${NC} Creating virtual serial port pair..."

# socat creates two linked PTYs. We capture their names from stderr.
# The -d -d flags enable debug output which shows the PTY paths.
SOCAT_OUTPUT=$(mktemp)
socat -d -d pty,raw,echo=0 pty,raw,echo=0 2>"$SOCAT_OUTPUT" &
SOCAT_PID=$!

# Give socat a moment to create the PTYs
sleep 1

# Verify socat is running
if ! kill -0 "$SOCAT_PID" 2>/dev/null; then
    echo -e "${RED}[ERROR]${NC} socat failed to start"
    cat "$SOCAT_OUTPUT"
    rm -f "$SOCAT_OUTPUT"
    exit 1
fi

# Extract PTY paths from socat's debug output
PTY_A=$(grep -o '/dev/pts/[0-9]*' "$SOCAT_OUTPUT" | head -1)
PTY_B=$(grep -o '/dev/pts/[0-9]*' "$SOCAT_OUTPUT" | tail -1)
rm -f "$SOCAT_OUTPUT"

if [ -z "$PTY_A" ] || [ -z "$PTY_B" ]; then
    echo -e "${RED}[ERROR]${NC} Failed to extract PTY paths from socat output"
    exit 1
fi

echo -e "${GREEN}[OK]${NC} Virtual serial port pair created:"
echo -e "       PTY A (UART program): ${CYAN}$PTY_A${NC}"
echo -e "       PTY B (test sender):  ${CYAN}$PTY_B${NC}"

# ---------------------------------------------------------------------------
# Step 2: Start a background sender that will feed data to the UART program
# ---------------------------------------------------------------------------
echo ""
echo -e "${CYAN}[STEP 2]${NC} Scheduling test data to be sent via $PTY_B..."

# We delay the sends so the UART program has time to start and transmit first
(
    sleep 2
    echo "Test message 1: RISC-V UART loopback OK" > "$PTY_B"
    sleep 1
    echo "Test message 2: ACT framework validation" > "$PTY_B"
    sleep 1
    echo "Test message 3: M-Mode firmware check"    > "$PTY_B"
    sleep 1
    printf '\x48\x65\x6C\x6C\x6F\x00\xDE\xAD' > "$PTY_B"  # Binary data test
) &
SENDER_PID=$!
echo -e "${GREEN}[OK]${NC} Sender scheduled (PID $SENDER_PID)"

# ---------------------------------------------------------------------------
# Step 3: Run the UART program on PTY A
# ---------------------------------------------------------------------------
echo ""
echo -e "${CYAN}[STEP 3]${NC} Running UART program on $PTY_A at $BAUD baud..."
echo -e "${CYAN}--------------------------------------------------------------${NC}"
echo ""

$BINARY "$PTY_A" "$BAUD"
EXIT_CODE=$?

echo ""
echo -e "${CYAN}--------------------------------------------------------------${NC}"

if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}[PASS]${NC} UART test completed successfully (exit code 0)"
else
    echo -e "${RED}[FAIL]${NC} UART test failed (exit code $EXIT_CODE)"
fi

exit $EXIT_CODE
