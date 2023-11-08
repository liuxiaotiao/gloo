     #ifndef QUICHE_H
#define QUICHE_H

#if defined(__cplusplus)
extern "C" {
#endif

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

// QUIC transport API.
//

// The current QUIC wire version.
// #define QUICHE_PROTOCOL_VERSION 0x00000001

// The maximum length of a connection ID.
// #define QUICHE_MAX_CONN_ID_LEN 20

// The minimum length of Initial packets sent by a client.
#define QUICHE_MIN_CLIENT_INITIAL_LEN 1200

enum quiche_error {
    // There is no more work to do.
    QUICHE_ERR_DONE = -1,

    // The provided buffer is too short.
    QUICHE_ERR_BUFFER_TOO_SHORT = -2,

    // The provided packet cannot be parsed because its version is unknown.
    QUICHE_ERR_UNKNOWN_VERSION = -3,

    // The provided packet cannot be parsed because it contains an invalid
    // frame.
    QUICHE_ERR_INVALID_FRAME = -4,

    // The provided packet cannot be parsed.
    QUICHE_ERR_INVALID_PACKET = -5,

    // The operation cannot be completed because the connection is in an
    // invalid state.
    QUICHE_ERR_INVALID_STATE = -6,

    // The operation cannot be completed because the stream is in an
    // invalid state.
    QUICHE_ERR_INVALID_STREAM_STATE = -7,

    // The peer's transport params cannot be parsed.
    QUICHE_ERR_INVALID_TRANSPORT_PARAM = -8,

    // A cryptographic operation failed.
    QUICHE_ERR_CRYPTO_FAIL = -9,

    // The TLS handshake failed.
    QUICHE_ERR_TLS_FAIL = -10,

    // The peer violated the local flow control limits.
    QUICHE_ERR_FLOW_CONTROL = -11,

    // The peer violated the local stream limits.
    QUICHE_ERR_STREAM_LIMIT = -12,

    // The specified stream was stopped by the peer.
    QUICHE_ERR_STREAM_STOPPED = -15,

    // The specified stream was reset by the peer.
    QUICHE_ERR_STREAM_RESET = -16,

    // The received data exceeds the stream's final size.
    QUICHE_ERR_FINAL_SIZE = -13,

    // Error in congestion control.
    QUICHE_ERR_CONGESTION_CONTROL = -14,
};



// Enables logging. |cb| will be called with log messages
// int quiche_enable_debug_logging(void (*cb)(const char *line, void *argp),
//                                 void *argp);

// Stores configuration shared between multiple connections.
typedef struct quiche_config quiche_config;

// Creates a config object with the given version.
quiche_config *quiche_config_new();




// Sets the `max_idle_timeout` transport parameter, in milliseconds, default is
// no timeout.
// void quiche_config_set_max_idle_timeout(quiche_config *config, uint64_t v);

// // Sets the `max_udp_payload_size transport` parameter.
// void quiche_config_set_max_recv_udp_payload_size(quiche_config *config, size_t v);

// // Sets the maximum outgoing UDP payload size.
// void quiche_config_set_max_send_udp_payload_size(quiche_config *config, size_t v);

// // Sets the `initial_max_data` transport parameter.
// void quiche_config_set_initial_max_data(quiche_config *config, uint64_t v);


// // Sets the `ack_delay_exponent` transport parameter.
// void quiche_config_set_ack_delay_exponent(quiche_config *config, uint64_t v);

// // Sets the `max_ack_delay` transport parameter.
// void quiche_config_set_max_ack_delay(quiche_config *config, uint64_t v);

// // Sets the `disable_active_migration` transport parameter.
// void quiche_config_set_disable_active_migration(quiche_config *config, bool v);

enum quiche_cc_algorithm {
    QUICHE_CC_NEWCUBIC = 1,
};



// Sets the congestion control algorithm used.
void quiche_config_set_cc_algorithm(quiche_config *config, enum quiche_cc_algorithm algo);

// Writes data to a stream.
ssize_t quiche_conn_write(quiche_conn *conn, 
                                const uint8_t *buf, size_t buf_len, ssize_t sent);

// Sets the maximum connection window.
void quiche_config_set_max_connection_window(quiche_config *config, uint64_t v);


// Frees the config object.
void quiche_config_free(quiche_config *config);

// Extracts version, type, source / destination connection ID and address
// verification token from the packet in |buf|.
int quiche_header_info(const uint8_t *buf, size_t buf_len, 
                        uint8_t *type);

// A QUIC connection.
typedef struct quiche_conn quiche_conn;

// Creates a new server-side connection.
quiche_conn *quiche_accept(const struct sockaddr *local, size_t local_len,
                           const struct sockaddr *peer, size_t peer_len,
                           quiche_config *config);

// Creates a new client-side connection.
quiche_conn *quiche_connect(const struct sockaddr *local, size_t local_len,
                            const struct sockaddr *peer, size_t peer_len,
                            quiche_config *config);

void quiche_dada_send(quiche_conn *conn,const char * data);

ssize_t quiche_conn_send_all(quiche_conn *conn);

typedef struct {
    // The remote address the packet was received from.
    struct sockaddr *from;
    socklen_t from_len;

    // The local address the packet was received on.
    struct sockaddr *to;
    socklen_t to_len;
} quiche_recv_info;

// Processes QUIC packets received from the peer.
ssize_t quiche_conn_recv(quiche_conn *conn, uint8_t *buf, size_t buf_len,
                         const quiche_recv_info *info);

typedef struct {
    // The local address the packet should be sent from.
    struct sockaddr_storage from;
    socklen_t from_len;

    // The remote address the packet should be sent to.
    struct sockaddr_storage to;
    socklen_t to_len;

} quiche_send_info;

// Writes a single QUIC packet to be sent to the peer.
ssize_t quiche_conn_send(quiche_conn *conn, uint8_t *out, size_t out_len,
                         quiche_send_info *out_info);

// Returns the size of the send quantum, in bytes.
// size_t quiche_conn_send_quantum(const quiche_conn *conn);

// // Reads contiguous data from a stream.
// ssize_t quiche_conn_stream_recv(quiche_conn *conn, uint64_t stream_id,
//                                 uint8_t *out, size_t buf_len, bool *fin);


// The side of the stream to be shut down.
enum quiche_shutdown {
    QUICHE_SHUTDOWN_READ = 0,
    QUICHE_SHUTDOWN_WRITE = 1,
};



// Returns the maximum possible size of egress UDP payloads.
size_t quiche_conn_max_send_udp_payload_size(const quiche_conn *conn);

// Returns the amount of time until the next timeout event, in nanoseconds.
uint64_t quiche_conn_timeout_as_nanos(const quiche_conn *conn);

// Returns the amount of time until the next timeout event, in milliseconds.
uint64_t quiche_conn_timeout_as_millis(const quiche_conn *conn);

// Processes a timeout event.
void quiche_conn_on_timeout(quiche_conn *conn);

// Closes the connection with the given error and reason.
int quiche_conn_close(quiche_conn *conn, bool app, uint64_t err,
                      const uint8_t *reason, size_t reason_len);



// Returns true if the connection handshake is complete.
bool quiche_conn_is_established(const quiche_conn *conn);



// Returns true if the connection is closed.
bool quiche_conn_is_closed(const quiche_conn *conn);

// Returns true if the connection was closed due to the idle timeout.
bool quiche_conn_is_timed_out(const quiche_conn *conn);



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

    // The maximum idle timeout.
    uint64_t peer_max_idle_timeout;

    // The maximum UDP payload size.
    uint64_t peer_max_udp_payload_size;

    // The initial flow control maximum data for the connection.
    uint64_t peer_initial_max_data;

    // The initial flow control maximum data for local bidirectional streams.
    uint64_t peer_initial_max_stream_data_bidi_local;

    // The initial flow control maximum data for remote bidirectional streams.
    uint64_t peer_initial_max_stream_data_bidi_remote;

    // The initial flow control maximum data for unidirectional streams.
    uint64_t peer_initial_max_stream_data_uni;

    // The initial maximum bidirectional streams.
    uint64_t peer_initial_max_streams_bidi;

    // The initial maximum unidirectional streams.
    uint64_t peer_initial_max_streams_uni;

    // The ACK delay exponent.
    uint64_t peer_ack_delay_exponent;

    // The max ACK delay.
    uint64_t peer_max_ack_delay;

    // Whether active migration is disabled.
    bool peer_disable_active_migration;

    // The active connection ID limit.
    uint64_t peer_active_conn_id_limit;

    // DATAGRAM frame extension parameter, if any.
    ssize_t peer_max_datagram_frame_size;
} quiche_stats;

// Collects and returns statistics about the connection.
void quiche_conn_stats(const quiche_conn *conn, quiche_stats *out);

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

    // The number of stream bytes retransmitted on this path.
    uint64_t stream_retrans_bytes;

    // The current PMTU for the path.
    size_t pmtu;

    // The most recent data delivery rate estimate in bytes/s.
    uint64_t delivery_rate;
} quiche_path_stats;


// Collects and returns statistics about the specified path for the connection.
//
// The `idx` argument represent the path's index (also see the `paths_count`
// field of `quiche_stats`).
int quiche_conn_path_stats(const quiche_conn *conn, size_t idx, quiche_path_stats *out);

// Returns the maximum DATAGRAM payload that can be sent.
// ssize_t quiche_conn_dgram_max_writable_len(const quiche_conn *conn);

// Returns the length of the first stored DATAGRAM.
// ssize_t quiche_conn_dgram_recv_front_len(const quiche_conn *conn);

// Returns the number of items in the DATAGRAM receive queue.
// ssize_t quiche_conn_dgram_recv_queue_len(const quiche_conn *conn);

// Returns the total size of all items in the DATAGRAM receive queue.
// ssize_t quiche_conn_dgram_recv_queue_byte_size(const quiche_conn *conn);

// Returns the number of items in the DATAGRAM send queue.
// ssize_t quiche_conn_dgram_send_queue_len(const quiche_conn *conn);

// Returns the total size of all items in the DATAGRAM send queue.
// ssize_t quiche_conn_dgram_send_queue_byte_size(const quiche_conn *conn);

// Reads the first received DATAGRAM.
// ssize_t quiche_conn_dgram_recv(quiche_conn *conn, uint8_t *buf,
//                                size_t buf_len);

// Sends data in a DATAGRAM frame.
// ssize_t quiche_conn_dgram_send(quiche_conn *conn, const uint8_t *buf,
//                                size_t buf_len);

// Purges queued outgoing DATAGRAMs matching the predicate.
// void quiche_conn_dgram_purge_outgoing(quiche_conn *conn,
//                                       bool (*f)(uint8_t *, size_t));

// Schedule an ack-eliciting packet on the active path.
ssize_t quiche_conn_send_ack_eliciting(quiche_conn *conn);

// Schedule an ack-eliciting packet on the specified path.
// ssize_t quiche_conn_send_ack_eliciting_on_path(quiche_conn *conn,
//                            const struct sockaddr *local, size_t local_len,
//                            const struct sockaddr *peer, size_t peer_len);

// Frees the connection object.
void quiche_conn_free(quiche_conn *conn);





#if defined(__cplusplus)
}  // extern C
#endif

#endif // QUICHE_H