# RISC-V ACT Framework — UART Communication Test Program

A Linux C program that initializes, configures, and communicates over a UART serial interface using the POSIX `termios` API. Designed for the LFX Mentorship coding challenge on M-Mode firmware validation.

## Features

- **UART Configuration** — Baud rate, 8 data bits, no parity, 1 stop bit (8N1) via `termios`
- **Transmit** — Sends a test message with short-write handling
- **Receive** — Non-blocking receive using `select()` with configurable timeout
- **Hex Dump** — Prints received data as both ASCII and hexadecimal for debugging
- **Error Handling** — Specific messages for permission issues, missing devices, and I/O failures
- **Signal Handling** — Graceful shutdown on `Ctrl+C` via `SIGINT`
- **No Hardware Required** — Full testing with `socat` virtual serial ports

## Project Structure

```
.
├── uart_comm.c          # Main UART program (well-commented)
├── Makefile             # Build system
├── test_loopback.sh     # Automated test using socat virtual serial ports
├── .gitignore           # Git ignore rules
└── README.md            # This file
```

## Prerequisites

**Linux system** with the following packages:

```bash
# Debian/Ubuntu
sudo apt update
sudo apt install gcc make socat

# Fedora/RHEL
sudo dnf install gcc make socat

# Arch Linux
sudo pacman -S gcc make socat
```

**Serial port permissions** (for physical devices):

```bash
sudo usermod -aG dialout $USER
# Log out and log back in for group changes to take effect
```

## Build Instructions

```bash
# Clone the repository
git clone https://github.com/<your-username>/riscv-uart-test.git
cd riscv-uart-test

# Build the program
make

# Or manually with gcc:
gcc -Wall -Wextra -Wpedantic -std=c11 -O2 -o uart_comm uart_comm.c
```

## Usage

```
./uart_comm [device_path] [baud_rate]
```

| Argument      | Default         | Description                              |
|---------------|-----------------|------------------------------------------|
| `device_path` | `/dev/ttyUSB0`  | Path to UART device or virtual PTY       |
| `baud_rate`   | `9600`          | Serial baud rate (1200–921600)           |

### Supported Baud Rates

`1200`, `2400`, `4800`, `9600`, `19200`, `38400`, `57600`, `115200`, `230400`, `460800`, `921600`

## Testing Without Hardware (Using socat)

[`socat`](http://www.dest-unreach.org/socat/) creates a pair of linked virtual serial ports (pseudo-terminals). Data written to one end appears on the other, simulating a physical UART connection.

### Automated Test

The included script handles everything automatically:

```bash
chmod +x test_loopback.sh
./test_loopback.sh
```

This will:
1. Create a virtual serial port pair with `socat`
2. Run the UART program on one port
3. Send multiple test messages (including binary data) from the other port
4. Display all transmitted and received data
5. Clean up all processes on exit

### Manual Test (3 Terminals)

**Terminal 1** — Create virtual serial port pair:
```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# Output will show two PTY paths, e.g.:
#   ... N PTY is /dev/pts/3
#   ... N PTY is /dev/pts/4
```

**Terminal 2** — Run the UART program on one PTY:
```bash
./uart_comm /dev/pts/3 9600
```

**Terminal 3** — Send data from the other PTY:
```bash
# Send a text message
echo "Hello from the other side!" > /dev/pts/4

# Send binary data
printf '\x48\x65\x6C\x6C\x6F' > /dev/pts/4
```

### Testing with Physical Hardware

```bash
# USB-to-serial adapter (most common)
./uart_comm /dev/ttyUSB0 115200

# Built-in serial port
./uart_comm /dev/ttyS0 9600

# FTDI adapter
./uart_comm /dev/ttyACM0 115200
```

## Example Output

```
=============================================================
  RISC-V ACT Framework — UART Communication Test Program
=============================================================
  Device:    /dev/pts/3
  Baud rate: 9600
  Config:    8N1 (8 data bits, no parity, 1 stop bit)
  Timeout:   5 seconds per receive cycle
=============================================================

[INFO] Opening UART device: /dev/pts/3
[INFO] UART device opened successfully (fd=3)
[INFO] UART configured: 9600 baud, 8N1, no flow control
[TX] Sending 43 bytes: "Hello from RISC-V ACT UART Test Program!
"
[TX] Successfully transmitted 43 bytes

[INFO] Entering receive loop (10 cycles, 5s timeout each)
[INFO] Press Ctrl+C to exit early

--- Receive cycle 1/10 ---
[RX] Received 27 bytes: "Hello from the other side!
"
[RX] Hex: 48 65 6C 6C 6F 20 66 72 6F 6D 20 74 68 65 20 6F 74 68 65 72 20 73 69 64 65 21 0A
--- Receive cycle 2/10 ---
[RX] Timeout after 5 seconds — no data received

=============================================================
  Session Summary
=============================================================
  Receive cycles: 2
  Bytes received: 27
  Exit reason:    User interrupt (Ctrl+C)
=============================================================
[INFO] UART device closed. Goodbye!
```

## Implementation Details

### termios Configuration

The program configures the serial port in **raw mode** with the following settings:

| Parameter       | Value     | termios Flag                |
|-----------------|-----------|-----------------------------|
| Data bits       | 8         | `CS8`                       |
| Parity          | None      | `~PARENB`                   |
| Stop bits       | 1         | `~CSTOPB`                   |
| Flow control    | None      | `~CRTSCTS`, `~IXON/IXOFF`  |
| Local mode      | Enabled   | `CLOCAL`                    |
| Receiver        | Enabled   | `CREAD`                     |
| Canonical mode  | Disabled  | `~ICANON`                   |
| Echo            | Disabled  | `~ECHO`                     |
| Signal chars    | Disabled  | `~ISIG`                     |

### Receive Mechanism

Instead of blocking `read()` or busy-wait polling, the program uses `select()`:

```
select() with timeout
    ├─ Data available → read() in a loop until drained
    ├─ Timeout        → report and retry next cycle
    └─ Error/Signal   → handle gracefully
```

This approach avoids wasting CPU cycles while still providing responsive data reception.

### Error Handling

| Error Condition    | Detection          | User Guidance                          |
|--------------------|--------------------|----------------------------------------|
| Permission denied  | `errno == EACCES`  | Suggests `dialout` group membership    |
| Device not found   | `errno == ENOENT`  | Suggests creating virtual port         |
| Invalid baud rate  | `B0` return        | Lists all supported rates              |
| Write failure      | `write() < 0`      | Reports errno with description         |
| Read failure       | `read() < 0`       | Distinguishes EAGAIN from real errors  |
| Signal interrupt   | `errno == EINTR`   | Retries the interrupted operation      |

## License

MIT
