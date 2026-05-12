/**
 * =============================================================================
 * RISC-V ACT Framework - UART Communication Program
 * =============================================================================
 *
 * Demonstrates Linux serial (UART) communication using the POSIX termios API.
 *
 * Features:
 *   1. Open and configure a UART device (physical or virtual via socat)
 *   2. Set UART parameters: baud rate, 8 data bits, no parity, 1 stop bit (8N1)
 *   3. Transmit a test message over UART
 *   4. Receive incoming data using select() with configurable timeout
 *   5. Print received data (ASCII + hex dump) to the console
 *   6. Graceful error handling for permissions, missing devices, I/O failures
 *
 * Usage:  ./uart_comm [device_path] [baud_rate]
 * Example: ./uart_comm /dev/pts/3 9600
 *
 * Author:  Sairam
 * License: MIT
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>

/* ---------------------------------------------------------------------------
 * Configuration Constants
 * ---------------------------------------------------------------------------*/
#define DEFAULT_DEVICE    "/dev/ttyUSB0"
#define DEFAULT_BAUD_RATE 9600
#define READ_TIMEOUT_SEC  5
#define RX_BUFFER_SIZE    256
#define TEST_MESSAGE      "Hello from RISC-V ACT UART Test Program!\r\n"
#define MAX_RX_CYCLES     10

/* ---------------------------------------------------------------------------
 * Global State — needed for signal handler cleanup
 * ---------------------------------------------------------------------------*/
static volatile int g_running = 1;
static int g_uart_fd = -1;

/* Signal handler: catches SIGINT for graceful shutdown */
static void signal_handler(int signum)
{
    (void)signum;
    fprintf(stderr, "\n[INFO] Caught interrupt signal, shutting down...\n");
    g_running = 0;
}

/* ---------------------------------------------------------------------------
 * baud_rate_to_speed_t()
 * Converts numeric baud rate to POSIX speed_t constant.
 * Returns B0 if the rate is unsupported.
 * ---------------------------------------------------------------------------*/
static speed_t baud_rate_to_speed_t(int baud_rate)
{
    switch (baud_rate) {
        case 1200:    return B1200;
        case 2400:    return B2400;
        case 4800:    return B4800;
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        case 460800:  return B460800;
        case 921600:  return B921600;
        default:      return B0;
    }
}

/* ---------------------------------------------------------------------------
 * uart_open()
 * Opens the UART device with O_RDWR | O_NOCTTY | O_NDELAY, then clears
 * the non-blocking flag (we use select() for timeout control instead).
 *
 * Provides specific error messages for EACCES and ENOENT.
 * Returns file descriptor on success, -1 on failure.
 * ---------------------------------------------------------------------------*/
static int uart_open(const char *device)
{
    int fd;

    printf("[INFO] Opening UART device: %s\n", device);

    fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        switch (errno) {
            case EACCES:
                fprintf(stderr, "[ERROR] Permission denied for '%s'.\n", device);
                fprintf(stderr, "        Try: sudo usermod -aG dialout $USER\n");
                break;
            case ENOENT:
                fprintf(stderr, "[ERROR] Device '%s' not found.\n", device);
                fprintf(stderr, "        Create a virtual port with socat:\n");
                fprintf(stderr, "        socat -d -d pty,raw,echo=0 pty,raw,echo=0\n");
                break;
            default:
                fprintf(stderr, "[ERROR] Failed to open '%s': %s (errno=%d)\n",
                        device, strerror(errno), errno);
                break;
        }
        return -1;
    }

    /* Switch back to blocking mode; we'll use select() for timeouts */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("[ERROR] fcntl F_GETFL failed");
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    printf("[INFO] UART device opened successfully (fd=%d)\n", fd);
    return fd;
}

/* ---------------------------------------------------------------------------
 * uart_configure()
 * Configures the UART with termios for 8N1 raw mode:
 *   c_cflag: 8 data bits, no parity, 1 stop bit, local mode, receiver on
 *   c_iflag: no SW flow control, no input processing
 *   c_oflag: raw output (no post-processing)
 *   c_lflag: raw input (no echo, no canonical mode, no signals)
 *   c_cc:    VMIN=0, VTIME=0 (non-blocking reads; select handles timeout)
 * Returns 0 on success, -1 on failure.
 * ---------------------------------------------------------------------------*/
static int uart_configure(int fd, int baud_rate)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "[ERROR] tcgetattr failed: %s\n", strerror(errno));
        return -1;
    }

    speed_t speed = baud_rate_to_speed_t(baud_rate);
    if (speed == B0) {
        fprintf(stderr, "[ERROR] Unsupported baud rate: %d\n", baud_rate);
        return -1;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* Control flags: 8N1, local, receiver enabled, no HW flow control */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Input flags: disable SW flow control and input processing */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                      INLCR | IGNCR | ICRNL);

    /* Output flags: raw output */
    tty.c_oflag &= ~OPOST;

    /* Local flags: raw mode (no echo, no canonical, no signals) */
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* Control characters: non-blocking reads */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[ERROR] tcsetattr failed: %s\n", strerror(errno));
        return -1;
    }

    tcflush(fd, TCIOFLUSH);

    printf("[INFO] UART configured: %d baud, 8N1, no flow control\n", baud_rate);
    return 0;
}

/* ---------------------------------------------------------------------------
 * uart_transmit()
 * Writes data to the UART, handling short writes and EINTR.
 * Calls tcdrain() to ensure all output is physically transmitted.
 * Returns 0 on success, -1 on failure.
 * ---------------------------------------------------------------------------*/
static int uart_transmit(int fd, const char *data, size_t len)
{
    size_t total_written = 0;

    printf("[TX] Sending %zu bytes: \"%s\"\n", len, data);

    while (total_written < len) {
        ssize_t n = write(fd, data + total_written, len - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[ERROR] UART write failed: %s\n", strerror(errno));
            return -1;
        }
        total_written += (size_t)n;
    }

    /* Block until all output has been physically transmitted */
    if (tcdrain(fd) != 0) {
        fprintf(stderr, "[WARN] tcdrain failed: %s\n", strerror(errno));
    }

    printf("[TX] Successfully transmitted %zu bytes\n", total_written);
    return 0;
}

/* ---------------------------------------------------------------------------
 * uart_receive()
 * Uses select() to wait for data with a timeout. When data is available,
 * reads it and prints both ASCII text and a hex dump.
 *
 * Returns: >0 bytes received, 0 on timeout, -1 on error.
 * ---------------------------------------------------------------------------*/
static int uart_receive(int fd, int timeout_sec)
{
    fd_set read_fds;
    struct timeval timeout;
    char rx_buffer[RX_BUFFER_SIZE];
    int total_received = 0;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    timeout.tv_sec  = timeout_sec;
    timeout.tv_usec = 0;

    int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);

    if (ret < 0) {
        if (errno == EINTR) {
            printf("[INFO] select() interrupted by signal\n");
            return 0;
        }
        fprintf(stderr, "[ERROR] select() failed: %s\n", strerror(errno));
        return -1;
    }

    if (ret == 0) {
        printf("[RX] Timeout after %d seconds — no data received\n", timeout_sec);
        return 0;
    }

    /* Data available — read until buffer is drained */
    if (FD_ISSET(fd, &read_fds)) {
        while (1) {
            memset(rx_buffer, 0, sizeof(rx_buffer));
            ssize_t n = read(fd, rx_buffer, sizeof(rx_buffer) - 1);

            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                fprintf(stderr, "[ERROR] UART read failed: %s\n", strerror(errno));
                return -1;
            }
            if (n == 0) break;

            rx_buffer[n] = '\0';
            printf("[RX] Received %zd bytes: \"%s\"\n", n, rx_buffer);

            /* Hex dump for debugging binary/non-printable data */
            printf("[RX] Hex: ");
            for (ssize_t i = 0; i < n; i++)
                printf("%02X ", (unsigned char)rx_buffer[i]);
            printf("\n");

            total_received += (int)n;
        }
    }

    return total_received;
}

/* ---------------------------------------------------------------------------
 * print_usage()
 * ---------------------------------------------------------------------------*/
static void print_usage(const char *prog)
{
    printf("Usage: %s [device_path] [baud_rate]\n", prog);
    printf("  device_path  UART device (default: %s)\n", DEFAULT_DEVICE);
    printf("  baud_rate    Baud rate   (default: %d)\n", DEFAULT_BAUD_RATE);
    printf("\nSupported baud rates: 1200-921600\n");
    printf("\nTest with socat:\n");
    printf("  socat -d -d pty,raw,echo=0 pty,raw,echo=0\n");
    printf("  %s /dev/pts/X 9600\n", prog);
}

/* ---------------------------------------------------------------------------
 * main()
 * ---------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    const char *device    = DEFAULT_DEVICE;
    int         baud_rate = DEFAULT_BAUD_RATE;

    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        device = argv[1];
    }
    if (argc >= 3) {
        baud_rate = atoi(argv[2]);
        if (baud_rate <= 0) {
            fprintf(stderr, "[ERROR] Invalid baud rate: '%s'\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    printf("=============================================================\n");
    printf("  RISC-V ACT Framework — UART Communication Test Program\n");
    printf("=============================================================\n");
    printf("  Device:    %s\n", device);
    printf("  Baud rate: %d\n", baud_rate);
    printf("  Config:    8N1 (8 data bits, no parity, 1 stop bit)\n");
    printf("  Timeout:   %d seconds per receive cycle\n", READ_TIMEOUT_SEC);
    printf("=============================================================\n\n");

    /* Install SIGINT handler for Ctrl+C */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        perror("[WARN] Failed to install SIGINT handler");

    /* Open the UART device */
    g_uart_fd = uart_open(device);
    if (g_uart_fd < 0) return EXIT_FAILURE;

    /* Configure UART parameters */
    if (uart_configure(g_uart_fd, baud_rate) != 0) {
        close(g_uart_fd);
        return EXIT_FAILURE;
    }

    /* Transmit a test message */
    if (uart_transmit(g_uart_fd, TEST_MESSAGE, strlen(TEST_MESSAGE)) != 0) {
        fprintf(stderr, "[ERROR] Failed to transmit test message\n");
        close(g_uart_fd);
        return EXIT_FAILURE;
    }

    /* Enter the receive loop */
    printf("\n[INFO] Entering receive loop (%d cycles, %ds timeout each)\n",
           MAX_RX_CYCLES, READ_TIMEOUT_SEC);
    printf("[INFO] Press Ctrl+C to exit early\n\n");

    int cycle = 0, total_bytes = 0;
    while (g_running && cycle < MAX_RX_CYCLES) {
        printf("--- Receive cycle %d/%d ---\n", cycle + 1, MAX_RX_CYCLES);
        int received = uart_receive(g_uart_fd, READ_TIMEOUT_SEC);
        if (received < 0) {
            fprintf(stderr, "[ERROR] Receive error, aborting\n");
            break;
        }
        total_bytes += received;
        cycle++;
    }

    /* Summary */
    printf("\n=============================================================\n");
    printf("  Session Summary\n");
    printf("=============================================================\n");
    printf("  Receive cycles: %d\n", cycle);
    printf("  Bytes received: %d\n", total_bytes);
    printf("  Exit reason:    %s\n",
           g_running ? "Completed all cycles" : "User interrupt (Ctrl+C)");
    printf("=============================================================\n");

    close(g_uart_fd);
    g_uart_fd = -1;
    printf("[INFO] UART device closed. Goodbye!\n");
    return EXIT_SUCCESS;
}
