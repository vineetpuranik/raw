// ============================================================================
// System / networking headers (POSIX/Linux/macOS)
// ----------------------------------------------------------------------------
// The following headers collectively expose the POSIX socket API and
// supporting facilities. Conceptually, the C standard library here acts
// as the user-space façade over a set of kernel syscalls (e.g., socket(2),
// bind(2), listen(2), accept(2), send(2), recv(2)) and ancillary helpers.
//
// <arpa/inet.h>
//   - Declares IPv4/IPv6 address conversion functions (inet_pton/inet_ntop)
//     and host/network byte-order utilities (htons/htonl).
//   - Rationale: Human-readable strings ("127.0.0.1") must be converted to
//     binary network-order representations (struct in_addr) for kernel APIs.
//
// <errno.h>
//   - Exposes the thread-local 'errno' integer. When a syscall or libc wrapper
//     fails, it returns -1 (or NULL) and writes an error code to errno.
//   - Rationale: Syscalls encode failure status out-of-band via errno.
//
// <netinet/in.h>
//   - Declares IPv4 address structures (struct sockaddr_in) and protocol
//     constants (AF_INET, IPPROTO_TCP). This header describes the *layout*
//     the kernel expects for IPv4 socket addresses.
//
// <signal.h>
//   - Declares signal primitives. We use it to ignore SIGPIPE so that a
//     failed send() to a peer that closed its socket yields EPIPE instead
//     of delivering a fatal signal to the process.
//
// <stdio.h>, <stdlib.h>, <string.h>
//   - Standard I/O, process control, and memory utilities. Used for logging,
//     exiting with well-defined status codes, and safe struct zero-initialization.
//
// <sys/socket.h>, <sys/types.h>
//   - Core socket API surface; data types like socklen_t. These headers define
//     the ABI between user space and kernel for socket-related syscalls.
//
// <unistd.h>
//   - POSIX OS services (close, read, write). Provides the canonical file
//     descriptor interface; sockets are file descriptors under the hood.
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// ============================================================================
// Protocol and resource parameters
// ----------------------------------------------------------------------------
// MAX_MSG_LEN
//   - Application-level constraint: we bound each echo request to ≤ 20 bytes.
//   - Motivation: Demonstrates defensive design against unbounded reads and
//     clarifies the server’s contract. Also caps per-connection buffer size.
//
// BACKLOG
//   - Argument to listen(2) controlling the length of the kernel’s SYN/accept
//     queue for this listening socket. If simultaneous connection attempts
//     exceed this backlog and the application is not accept()’ing fast enough,
//     the kernel may refuse additional connections (or clients experience delay).
//   - Note: The kernel may cap this value (e.g., somaxconn). 128 is a modest default.
#define MAX_MSG_LEN 20
#define BACKLOG 128

// ============================================================================
// Error termination helper
// ----------------------------------------------------------------------------
// die(const char*)
//   - Prints a human-readable diagnostic (perror consults errno) and exits
//     with a failure code. This is acceptable for a minimal server; robust
//     servers prefer structured error handling and graceful shutdown.
//
// Under the hood:
//   - perror() formats the string "<msg>: <strerror(errno)>\n" to stderr.
//   - exit(EXIT_FAILURE) performs atexit handlers and terminates the process.
static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    // =========================================================================
    // 1) Configuration: which local address/port to bind
    // -------------------------------------------------------------------------
    // Defaults:
    //   - IP  "0.0.0.0" binds to INADDR_ANY: the kernel will accept connections
    //     arriving on any local interface (loopback, ethernet, etc.).
    //   - Port 9000 is an arbitrary user-space port (not privileged).
    //
    // Rationale:
    //   - Separating configuration from mechanics improves testability.
    //   - Accepting overrides via argv enables flexible deployment (e.g., Docker).
    const char *bind_ip = "0.0.0.0";
    int port = 9000;

    if (argc >= 2) {
        bind_ip = argv[1];            // e.g., "127.0.0.1" to restrict to loopback
    }
    if (argc >= 3) {
        port = atoi(argv[2]);         // simplistic parse; production would validate
    }

    // =========================================================================
    // 2) Signal semantics: avoid process termination on broken pipe
    // -------------------------------------------------------------------------
    // Problem:
    //   - When the peer half-closes the connection and we subsequently send(),
    //     POSIX may raise SIGPIPE. Default disposition is to terminate the process.
    //
    // Strategy:
    //   - Ignore SIGPIPE so send() fails with -1 and errno=EPIPE, letting us
    //     handle errors explicitly in program logic instead of via signal death.
    signal(SIGPIPE, SIG_IGN);

    // =========================================================================
    // 3) Create the listening socket (endpoint in the local kernel)
    // -------------------------------------------------------------------------
    // socket(domain=AF_INET, type=SOCK_STREAM, protocol=0)
    //   - domain   : IPv4 protocol family (AF_INET).
    //   - type     : STREAM semantics (TCP: reliable, ordered, byte-stream).
    //   - protocol : 0 lets the kernel select IPPROTO_TCP for AF_INET/STREAM.
    //
    // Kernel-level view:
    //   - Allocates a socket descriptor in the process table (an int fd).
    //   - Initializes kernel-side state machines for TCP (closed state).
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        die("socket");
    }

    // =========================================================================
    // 4) Tuning: allow quick rebinding after restart
    // -------------------------------------------------------------------------
    // SO_REUSEADDR
    //   - Without this, a recently-closed TCP port may be stuck in TIME_WAIT and
    //     bind() can fail with EADDRINUSE. This option allows faster development
    //     cycles by relaxing certain checks for local address reuse.
    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        die("setsockopt");
    }

    // =========================================================================
    // 5) Materialize the bind address (userspace struct -> kernel ABI layout)
    // -------------------------------------------------------------------------
    // sockaddr_in is the IPv4-specific socket address; the kernel expects an
    // opaque pointer to struct sockaddr whose first bytes match this layout.
    //
    // Byte order:
    //   - Ports on the wire and in the kernel ABI are big-endian (network order).
    //     htons() converts from host endianness to network endianness.
    //
    // Address conversion:
    //   - inet_pton(AF_INET, "A.B.C.D", &addr.sin_addr) parses dotted-quad text
    //     into a 32-bit in_addr (network byte order).
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));           // eliminate uninitialized padding
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        die("inet_pton");
    }

    // =========================================================================
    // 6) Bind: associate the socket with a local (ip,port)
    // -------------------------------------------------------------------------
    // bind(fd, sockaddr*, len)
    //   - Informs the kernel of our chosen local address. After success, the
    //     socket is “named” and visible to the TCP/IP stack for incoming SYNs.
    // Failure modes:
    //   - EADDRINUSE if another socket already bound the same 4-tuple (or
    //     conflicts via SO_REUSEADDR semantics). EACCES if binding a privileged port.
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("bind");
    }

    // =========================================================================
    // 7) Listen: transition to passive (server) mode
    // -------------------------------------------------------------------------
    // listen(fd, backlog)
    //   - Notifies the kernel that we intend to accept incoming connections.
    //   - Creates/adjusts the accept queue whose capacity is guided by 'backlog'.
    //   - TCP state machine for this socket becomes “LISTEN”.
    if (listen(s, BACKLOG) < 0) {
        die("listen");
    }

    printf("⚡ raw TCP server listening on %s:%d (max %d chars per message)\n",
           bind_ip, port, MAX_MSG_LEN);

    // =========================================================================
    // 8) Accept loop: convert pending SYNs into connected sockets
    // -------------------------------------------------------------------------
    // Model:
    //   - The listening socket 's' remains in LISTEN state. Each successful
    //     accept() returns a *new* connected socket descriptor 'cfd' bound
    //     to the 5-tuple (src IP/port, dst IP/port, protocol) for that client.
    //   - 'cfd' is independent of 's'; closing 'cfd' does not affect 's'.
    //
    // Blocking semantics:
    //   - accept() blocks by default until the kernel dequeues a connection
    //     from the accept queue. EINTR indicates interruption by a signal.
    for (;;) {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);

        int cfd = accept(s, (struct sockaddr *)&cli, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;      // retry on signal interruption
            perror("accept");                  // transient errors logged; continue serving
            continue;
        }

        // Optional: observe peer address (for logging/diagnostics).
        // inet_ntop converts the binary address back to presentation format.
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, client_ip, sizeof(client_ip));
        printf("👋  client connected from %s:%d\n", client_ip, ntohs(cli.sin_port));

        // ---------------------------------------------------------------------
        // Per-connection handling (to be implemented next):
        //   - Read at most MAX_MSG_LEN application bytes (enforce protocol).
        //   - If the message exceeds the bound, drain input and respond with an
        //     error sentinel; otherwise echo the payload.
        //
        // Low-level I/O notes:
        //   - recv(cfd, buf, n, 0) reads from the TCP receive buffer maintained
        //     by the kernel for this connection. Short reads are possible.
        //   - send(cfd, buf, n, 0) enqueues data into the TCP send buffer; the
        //     kernel handles segmentation, retransmission, and congestion control.
        //
        // Robust servers would loop until newline or EOF, carefully handling:
        //   * partial reads/writes
        //   * EAGAIN/EWOULDBLOCK if using nonblocking I/O
        //   * connection half-closes (peer shutdown(SHUT_WR))
        //

        // =========================================================================
        // 9) Per-connection message processing
        // -------------------------------------------------------------------------
        // Goal:
        //   Implement a simple request–response echo protocol:
        //     - Read up to MAX_MSG_LEN bytes or until newline ('\n').
        //     - If client sends > MAX_MSG_LEN bytes before newline,
        //       discard the rest and respond with "ERR too long\n".
        //     - Otherwise, echo the received bytes back verbatim.
        //
        // Design reasoning:
        //   - recv() operates on the TCP receive buffer managed by the kernel.
        //     Each call may return fewer bytes than requested; therefore, we
        //     accumulate until newline or size limit.
        //   - TCP is a stream protocol, not message-oriented — it preserves
        //     byte order but not boundaries. Hence the explicit loop below.
        //
        char buf[MAX_MSG_LEN + 1];    // +1 for NUL terminator (for safe printing)
        ssize_t total = 0;            // number of valid bytes accumulated
        int too_long = 0;             // flag if message exceeds MAX_MSG_LEN

        for (;;) {
            char ch;
            ssize_t n = recv(cfd, &ch, 1, 0);  // read one byte at a time
            if (n == 0) {
                // Peer performed an orderly shutdown (sent FIN).
                // The kernel’s receive queue is empty and the connection closed.
                break;
            } else if (n < 0) {
                // recv() failed; EINTR means interrupted by a signal.
                if (errno == EINTR) continue;
                perror("recv");
                break;
            }

            // Detect newline terminator (protocol boundary)
            if (ch == '\n' || ch == '\r') {
                break;
            }

            // If message still within allowed bound, store it
            if (total < MAX_MSG_LEN) {
                buf[total++] = ch;
            } else {
                // Message too long: continue draining input
                too_long = 1;
            }
        }

        buf[total] = '\0';  // ensure string safety for printing/logging

        // -------------------------------------------------------------------------
        // Response path:
        //   - If input exceeded MAX_MSG_LEN before newline, emit an error.
        //   - Otherwise, echo the content exactly as received.
        // send() semantics:
        //   - Copies user-space bytes into the kernel’s send buffer. The kernel
        //     handles segmentation and retransmission transparently.
        //   - On EPIPE, the peer closed its read side; ignore gracefully.
        if (too_long) {
            const char *err = "ERR too long\n";
            ssize_t wn = send(cfd, err, strlen(err), 0);
            if (wn < 0 && errno != EPIPE)
                perror("send error message");
            printf("⚠️  client sent overlong message; error sent\n");
        } else if (total > 0) {
            ssize_t wn = send(cfd, buf, total, 0);
            if (wn < 0 && errno != EPIPE)
                perror("send echo");
            else
                send(cfd, "\n", 1, 0);  // append newline for readability
            printf("🔁  echoed \"%s\" (%zd bytes)\n", buf, total);
        } else {
            // Empty or connection closed before sending data.
            printf("ℹ️  connection closed with no data\n");
        }


        close(cfd);  // Return the connected socket’s resources to the kernel.
                     // This sends a FIN (orderly close) once unsent data is flushed.
    }

    // Unreachable in this minimal server; a graceful shutdown would:
    //   - Close the listening socket, drain inflight connections, release resources.
    //   - Consider SIGTERM handling and an accept() wakeup strategy.


}
