#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#endif

#ifdef __unix__
#include <sys/types.h>
#endif
#ifdef _MSC_VER
#include <BaseTsd.h>
#define ssize_t SSIZE_T
#endif
extern "C" {

enum dmludp_error {
    // There is no more work to do.
    DMLUDP_ERR_DONE = -1,

    // The provided buffer is too short.
    DMLUDP_ERR_BUFFER_TOO_SHORT = -2,

    // The provided packet cannot be parsed because its version is unknown.
    DMLUDP_ERR_UNKNOWN_VERSION = -3,

    // The provided packet cannot be parsed because it contains an invalid
    // frame.
    DMLUDP_ERR_INVALID_FRAME = -4,

    // The provided packet cannot be parsed.
    DMLUDP_ERR_INVALID_PACKET = -5,

    // The operation cannot be completed because the connection is in an
    // invalid state.
    DMLUDP_ERR_INVALID_STATE = -6,

    // The operation cannot be completed because the stream is in an
    // invalid state.
    DMLUDP_ERR_INVALID_STREAM_STATE = -7,

    // The peer's transport params cannot be parsed.
    DMLUDP_ERR_INVALID_TRANSPORT_PARAM = -8,

    // A cryptographic operation failed.
    DMLUDP_ERR_CRYPTO_FAIL = -9,

    // The TLS handshake failed.
    DMLUDP_ERR_TLS_FAIL = -10,

    // The peer violated the local flow control limits.
    DMLUDP_ERR_FLOW_CONTROL = -11,

    // The peer violated the local stream limits.
    DMLUDP_ERR_STREAM_LIMIT = -12,

    // The specified stream was stopped by the peer.
    DMLUDP_ERR_STREAM_STOPPED = -15,

    // The specified stream was reset by the peer.
    DMLUDP_ERR_STREAM_RESET = -16,

    // The received data exceeds the stream's final size.
    DMLUDP_ERR_FINAL_SIZE = -13,

    // Error in congestion control.
    DMLUDP_ERR_CONGESTION_CONTROL = -14,

    // Too many identifiers were provided.
    DMLUDP_ERR_ID_LIMIT = -17,

    // Not enough available identifiers.
    DMLUDP_ERR_OUT_OF_IDENTIFIERS = -18,

    // Error in key update.
    DMLUDP_ERR_KEY_UPDATE = -19,
};


// Stores configuration shared between multiple connections.
typedef struct dmludp_config dmludp_config;

// Creates a config object with the given version.
dmludp_config *dmludp_config_new();

enum dmludp_cc_algorithm {
    DMLUDP_CC_NEWCUBIC = 1,
};

// Sets the congestion control algorithm used.
void dmludp_config_set_cc_algorithm(dmludp_config *config, enum dmludp_cc_algorithm algo);

// Frees the config object.
void dmludp_config_free(dmludp_config *config);

////////////////////////////////////////
// Extracts version, type, source / destination connection ID and address
// verification token from the packet in |buf|.
int dmludp_header_info(const uint8_t *buf, size_t buf_len, uint8_t *type, int *pkt_num);


typedef struct dmludp_conn dmludp_conn;

// Creates a new server-side connection.
dmludp_conn *dmludp_accept(const struct sockaddr *local, size_t local_len,
                           const struct sockaddr *peer, size_t peer_len,
                           dmludp_config *config);

// Creates a new client-side connection.
dmludp_conn *dmludp_connect(const struct sockaddr *local, size_t local_len,
                            const struct sockaddr *peer, size_t peer_len,
                            dmludp_config *config);

void dmludp_data_write(dmludp_conn *conn, const uint8_t *buf, size_t buf_len);

typedef struct {
    // The remote address the packet was received from.
    struct sockaddr *from;
    socklen_t from_len;

    // The local address the packet was received on.
    struct sockaddr *to;
    socklen_t to_len;
} dmludp_recv_info;

// Processes QUIC packets received from the peer.
// ssize_t dmludp_conn_recv(dmludp_conn *conn, uint8_t *buf, size_t buf_len,
//                          const dmludp_recv_info *info);
ssize_t dmludp_conn_recv(dmludp_conn *conn, uint8_t *buf, size_t buf_len);

typedef struct {
    // The local address the packet should be sent from.
    struct sockaddr_storage from;
    socklen_t from_len;

    // The remote address the packet should be sent to.
    struct sockaddr_storage to;
    socklen_t to_len;

} dmludp_send_info;

// Writes a single QUIC packet to be sent to the peer.
ssize_t dmludp_conn_send(dmludp_conn *conn, uint8_t *out, size_t out_len,
                         dmludp_send_info *out_info);

ssize_t dmludp_send_data_stop(dmludp_conn *conn, uint8_t *out, size_t out_len);

ssize_t dmludp_send_data_handshake(dmludp_conn *conn, uint8_t *out, size_t out_len);

// Store application into dmludp

bool dmludp_conn_send_all(dmludp_conn *conn);

// void dmludp_data_send(dmludp_conn *conn, const char* buf);
// Get rtt
double dmludp_get_rtt(dmludp_conn *conn);

bool dmludp_conn_is_stop(dmludp_conn *conn);
// bool dmludp_conn_empty(dmludp_conn *conn);

typedef struct {
    // The number of QUIC packets received on this connection.
    size_t recv;

    // The number of QUIC packets sent on this connection.
    size_t sent;

    // The number of QUIC packets that were lost.
    size_t lost;

    // The number of sent QUIC packets with retransmitted data.
    size_t retrans;

    // The number of sent bytes.
    uint64_t sent_bytes;

    // The number of received bytes.
    uint64_t recv_bytes;

    // The number of bytes lost.
    uint64_t lost_bytes;

    // The number of stream bytes retransmitted.
    uint64_t stream_retrans_bytes;

    // The number of known paths for the connection.
    size_t paths_count;
} dmludp_stats;


typedef struct {
    // The local address used by this path.
    struct sockaddr_storage local_addr;
    socklen_t local_addr_len;

    // The peer address seen by this path.
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;

    // The validation state of the path.
    ssize_t validation_state;

    // Whether this path is active.
    bool active;

    // The number of QUIC packets received on this path.
    size_t recv;

    // The number of QUIC packets sent on this path.
    size_t sent;

    // The number of QUIC packets that were lost on this path.
    size_t lost;

    // The number of sent QUIC packets with retransmitted data on this path.
    size_t retrans;

    // The estimated round-trip time of the path (in nanoseconds).
    uint64_t rtt;

    // The size of the path's congestion window in bytes.
    size_t cwnd;

    // The number of sent bytes on this path.
    uint64_t sent_bytes;

    // The number of received bytes on this path.
    uint64_t recv_bytes;

    // The number of bytes lost on this path.
    uint64_t lost_bytes;

    // The most recent data delivery rate estimate in bytes/s.
    uint64_t delivery_rate;
} dmludp_path_stats;

bool dmludp_conn_is_empty(dmludp_conn *conn);

bool dmludp_buffer_is_empty(dmludp_conn *conn);

ssize_t dmludp_data_read(dmludp_conn *conn, uint8_t *out, size_t out_len);

// Returns whether or not this is a server-side connection.
// bool dmludp_conn_is_server(const dmludp_conn *conn);

// Frees the connection object.
void dmludp_conn_free(dmludp_conn *conn);

void dmludp_set_rtt(dmludp_conn *conn, long interval);

}
