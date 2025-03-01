#pragma once
#include <cstring>
#include <chrono>
#include "gloo/connection.h"
#include "gloo/recv_buf.h"
#include "gloo/send_buf.h"
#include "gloo/packet.h"


using namespace dmludp;

using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Priority_len = uint8_t;

using Offset_len = uint64_t;

using Packet_len = uint64_t;

enum dmludp_error {
    // There is no more work to do.
    DMLUDP_ERR_DONE = -1,

    // The provided buffer is too short.
    DMLUDP_ERR_BUFFER_TOO_SHORT = -2,

    // The provided buffer is transmitted.
    DMLUDP_ERR_STOP = -3,

    // The operation cannot be completed because the connection is in an
    // invalid state.
    DMLUDP_ERR_INVALID_STATE = -6,

    // The peer's transport params cannot be parsed.
    DMLUDP_ERR_INVALID_TRANSPORT_PARAM = -8,

    // A cryptographic operation failed.
    DMLUDP_ERR_CRYPTO_FAIL = -9,

    // The TLS handshake failed.
    DMLUDP_ERR_TLS_FAIL = -10,

    // The peer violated the local flow control limits.
    DMLUDP_ERR_FLOW_CONTROL = -11,


    // The received data exceeds the stream's final size.
    DMLUDP_ERR_FINAL_SIZE = -13,

    // Error in congestion control.
    DMLUDP_ERR_CONGESTION_CONTROL = -14,

    // Too many identifiers were provided.
    DMLUDP_ERR_ID_LIMIT = -17,

    // Not enough available identifiers.
    DMLUDP_ERR_OUT_OF_IDENTIFIERS = -18,

};

inline Config* dmludp_config_new(){
    Config* config = new Config();
    return config;
}

inline void dmludp_config_free(Config* config){
    delete config;
}

inline int dmludp_header_info(uint8_t* data, size_t buf_len, uint32_t &off, uint64_t &pn) {
    auto result = reinterpret_cast<Header *>(data)->ty;
    pn = reinterpret_cast<Header *>(data)->pkt_num;
    // auto pkt_priorty = reinterpret_cast<Header *>(data)->priority;
    off = reinterpret_cast<Header *>(data)->offset;
    // auto pkt_len = reinterpret_cast<Header *>(data)->pkt_length;
    return result;
}

// inline int dmludp_process_header_info(std::shared_ptr<Connection> conn, uint8_t* data, size_t buf_len, uint32_t &off, uint64_t &pn) {
//     return conn->pre_process_application_packet(data, buf_len, off, pn);
// }

// inline Connection* dmludp_accept(sockaddr_storage local, sockaddr_storage peer, Config config) {
inline std::shared_ptr<Connection> dmludp_accept(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return dmludp::Connection::accept(local, peer, config);
}

// inline Connection* dmludp_connect(sockaddr_storage local, sockaddr_storage peer, Config config) {
inline std::shared_ptr<Connection> dmludp_connect(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return dmludp::Connection::connect(local, peer, config);
}

inline void dmludp_update_receive_parameters(std::shared_ptr<Connection> conn){
    conn->update_receive_parameter();
}

// inline void dmludp_set_rtt(Connection* conn, long interval){
inline void dmludp_set_rtt(std::shared_ptr<Connection> conn, long interval){
    conn->set_rtt(interval);
}

inline void dmludp_conn_set_send_time(std::shared_ptr<Connection> conn){
    conn->set_send_time();
}

inline bool dmludp_get_data(std::shared_ptr<Connection> conn, struct iovec *iovecs, int iovecs_len){
    return conn->get_data(iovecs, iovecs_len);
}

inline size_t dmludp_get_error_sent(std::shared_ptr<Connection> conn){
    return conn->get_error_sent();
}

// inline ssize_t dmludp_data_send_msg(std::shared_ptr<Connection> conn, 
//     std::vector<std::shared_ptr<Header>> &hdrs, 
//     std::vector<struct msghdr> &messages, 
//     std::vector<struct iovec> &iovecs,
//     std::vector<std::vector<uint8_t>> &out_ack){
//     return conn->send_mmsg(hdrs, messages, iovecs, out_ack);
// }

// inline ssize_t dmludp_data_send_msg(std::shared_ptr<Connection> conn, 
//     std::vector<std::shared_ptr<Header>> &hdrs, 
//     std::vector<struct msghdr> &messages, 
//     std::vector<struct iovec> &iovecs,
//     std::vector<std::vector<uint8_t>> &out_ack){
//     return conn->send_msg(hdrs, messages, iovecs, out_ack);
// }

// inline ssize_t dmludp_data_send_partial_msg(std::shared_ptr<Connection> conn, 
//     std::vector<std::shared_ptr<Header>> &hdrs, 
//     std::vector<struct msghdr> &messages, 
//     std::vector<struct iovec> &iovecs){
//     return conn->send_partial_mmsg(hdrs, messages, iovecs);
// }

inline bool dmludp_transmission_complete(std::shared_ptr<Connection> conn){
    return conn->transmission_complete();
}

// inline ssize_t dmludp_send_data_acknowledge(std::shared_ptr<Connection> conn, uint8_t* out, size_t out_len){
//     return conn->send_data_acknowledge(out, out_len);

// }


// inline void dmludp_conn_recovery(std::shared_ptr<Connection> conn){
//     conn->recovery_send_buffer();
// }

inline long dmludp_get_rtt(std::shared_ptr<Connection> conn){
    return conn->get_rtt();
}

// inline ssize_t dmludp_send_data_stop(Connection* conn, uint8_t* out, size_t out_len){
inline ssize_t dmludp_send_data_stop(std::shared_ptr<Connection> conn, uint8_t* out, size_t out_len){
    if (out_len == 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    size_t written = conn->send_data_stop(out);
    return static_cast<ssize_t>(written);
}

inline ssize_t dmludp_send_data_handshake(std::shared_ptr<Connection> conn, uint8_t* out, size_t out_len){
    if (out_len == 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    size_t written = conn->send_data_handshake(out);
    return static_cast<ssize_t>(written);
}

// inline bool dmludp_conn_is_closed(Connection* conn){
inline bool dmludp_conn_is_closed(std::shared_ptr<Connection> conn){
    return conn->is_closed();
}

// check recv buffer if empty
// inline bool dmludp_conn_has_recv(std::shared_ptr<Connection> conn){
//     return conn->has_recv();
// }


inline size_t dmludp_conn_data_sent_once(std::shared_ptr<Connection> conn){
    return conn->get_once_data_len();
}

// inline ssize_t dmludp_conn_send(Connection* conn, uint8_t* out, size_t out_len) {
inline ssize_t dmludp_conn_send(std::shared_ptr<Connection> conn, uint8_t* out, size_t out_len) {
    if(out_len == 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    size_t written = conn->send_data(out);
    if (written > 0){
        return static_cast<ssize_t>(written);
    }

    if (conn->is_stopped()) {
        return dmludp_error::DMLUDP_ERR_DONE;
    }
    return written;
}


// inline ssize_t dmludp_conn_recv(Connection* conn, const uint8_t* buf, size_t out_len){
inline ssize_t dmludp_conn_recv(std::shared_ptr<Connection> conn, uint8_t* buf, size_t out_len){
    if(out_len == 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }
    
    size_t received = conn->recv_slice(buf, out_len);
    
    if (received == 0){
        uint64_t pkt_num;
        uint32_t pkt_offset;
        uint8_t pkt_difference;
        uint16_t pkt_len;
        auto ty = conn->header_info(buf, out_len, pkt_num, pkt_offset, pkt_difference, pkt_len);
        if (ty == Type::Stop){
            return dmludp_error::DMLUDP_ERR_STOP;
        }

        if (ty == Type::Fin){
            return dmludp_error::DMLUDP_ERR_DONE;
        }

    }
    return static_cast<ssize_t>(received);

}

// inline bool dmludp_check_first_entry(std::shared_ptr<Connection> conn, size_t check_len){
//     return conn->check_first_entry(check_len);
// }

// inline void dmludp_conn_recv_padding(std::shared_ptr<Connection> conn, size_t total_len){
//     return conn->recv_padding(total_len);
// }

// inline void dmludp_conn_recv_reset(std::shared_ptr<Connection> conn){
//     conn->recv_reset();
// }

// inline size_t dmludp_conn_recv_len(std::shared_ptr<Connection> conn){
//     return conn->recv_len();
// }

// inline ssize_t dmludp_data_read(Connection* conn, uint8_t* buf, size_t len){
// inline ssize_t dmludp_data_read(std::shared_ptr<Connection> conn, void* buf, size_t len, bool iscopy = false){
//     if(len == 0){
//         return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
//     }
//     size_t result = conn->read(static_cast<uint8_t*>(buf), iscopy, len);

//     return static_cast<ssize_t>(result);
// }

inline void dmludp_set_error(std::shared_ptr<Connection> conn, size_t err, size_t sent = 0){
    conn->set_error(err, sent);
}

inline size_t dmludp_get_dmludp_error(std::shared_ptr<Connection> conn){
    return conn->get_dmludp_error();
}

inline void dmludp_clear_recv_setting(std::shared_ptr<Connection> conn){
    conn->clear_recv_setting();
}

inline void dmludp_conn_clear_sent_once(std::shared_ptr<Connection> conn){
    conn->clear_sent_once();
}

inline bool dmludp_conn_receive_complete(std::shared_ptr<Connection> conn){
    return conn->receive_complete();
}

inline void dmludp_conn_rx_len(std::shared_ptr<Connection> conn, size_t expected){
    conn->rx_len(expected);
}

inline void dmludp_conn_reset_rx_len(std::shared_ptr<Connection> conn){
    conn->reset_rx_len();
}

