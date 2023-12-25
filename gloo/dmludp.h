#pragma once
#include <cstring>
#include "gloo/connection.h"
#include "gloo/RangeBuf.h"
#include "gloo/recv_buf.h"
#include "gloo/send_buf.h"
#include "gloo/packet.h"


using namespace dmludp;
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

inline int dmludp_header_info(uint8_t* data, size_t buf_len, int &type, int &pn) {
    std::vector<uint8_t> buf(data, data + buf_len);
    auto hdr = Header::from_slice(buf);
    pn = hdr->pkt_num;
    int result = 0;
    if (hdr->ty == Type::Retry){
        result = 1;
    }else if(hdr->ty == Type::Handshake){
        result = 2;
    }else if(hdr->ty == Type::Application){
        result = 3;
    }else if(hdr->ty == Type::ElictAck){
        result = 4;
    }else if(hdr->ty == Type::ACK){
        result = 5;
    }else if(hdr->ty == Type::Stop){
        result = 6;
    }else if(hdr->ty == Type::Fin){
        result = 7;
    }else if(hdr->ty == Type::StartAck){
        result = 8;
    }
    return result;
}

inline Connection* dmludp_accept(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return dmludp::Connection::accept(local, peer, config);
}

inline Connection* dmludp_connect(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return dmludp::Connection::connect(local, peer, config);
}

inline void dmludp_set_rtt(Connection* conn, long interval){
    conn->set_rtt(interval);
}

// Write data from application to protocal
inline void dmludp_data_write(Connection* conn, const uint8_t* buf, size_t len){
    conn->data_write(buf, len);
}

// Fill up congestion control window
inline bool dmludp_conn_send_all(Connection* conn) {
    return conn->send_all();
}

inline bool dmludp_conn_is_empty(Connection* conn){
    return conn->data_is_empty();
}

inline long dmludp_get_rtt(Connection* conn){
    return conn->get_rtt();
}


inline ssize_t dmludp_send_data_stop(Connection* conn, uint8_t* out, size_t out_len){
    if (out_len <= 0 ){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> out_vector(out_len);
    size_t written = conn->send_data_stop(out_vector);
    memcpy(out, out_vector.data(), out_len);
    return static_cast<ssize_t>(written);
}

inline bool dmludp_buffer_is_empty(Connection* conn){
    return conn->is_empty();
}

inline bool dmludp_conn_is_stop(Connection* conn){
    return conn->is_stopped();
}

inline ssize_t dmludp_send_data_handshake(Connection* conn, uint8_t* out, size_t out_len){
    if (out_len <= 0 ){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> out_vector(out_len);
    size_t written = conn->send_data_handshake(out_vector);
    std::copy(out_vector.begin(), out_vector.begin() + written, out);
    // memcpy(out, out_vector.data(), written);
    return static_cast<ssize_t>(written);
}

inline bool dmludp_conn_is_closed(Connection* conn){
    return conn->is_closed();
}

inline void dmludp_conn_free(Connection* conn) {
    delete conn;
}


inline ssize_t dmludp_conn_send(Connection* conn, uint8_t* out, size_t out_len) {
    if(out_len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    if (conn->is_stopped()) {
        return dmludp_error::DMLUDP_ERR_DONE;
    }

    std::vector<uint8_t> buf(out_len);

    size_t written = conn->send_data(buf);
    memcpy(out, buf.data(), out_len);

    return static_cast<ssize_t>(written);
}


inline ssize_t dmludp_conn_recv(Connection* conn, const uint8_t* buf, size_t out_len){
    if(out_len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> recv_buf(buf, buf + out_len);

    size_t received = conn->recv_slice(recv_buf);
    
    if (received == 0){
        auto hdr = Header::from_slice(recv_buf);
        auto ty = hdr->ty;
        if (hdr->ty == Type::Stop){
            return dmludp_error::DMLUDP_ERR_STOP;
        }

        if (hdr->ty == Type::Fin){
            return dmludp_error::DMLUDP_ERR_DONE;
        }

    }

    return static_cast<ssize_t>(received);

}

inline ssize_t dmludp_data_read(Connection* conn, uint8_t* buf, size_t len){
    if(len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> data_slice(buf, buf+len);

    size_t result = conn->read(data_slice);

    return static_cast<ssize_t>(result);
}