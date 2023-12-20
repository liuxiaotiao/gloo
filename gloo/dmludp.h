#pragma once
#include "gloo/connection.h"
#include "gloo/RangeBuf.h"
#include "gloo/recv_buf.h"
#include "gloo/send_buf.h"
#include "gloo/packet.h"

#include <s>

using namespace dmludp;
enum dmludp_error {
    // There is no more work to do.
    DMLUDP_ERR_DONE = -1,

    // The provided buffer is too short.
    DMLUDP_ERR_BUFFER_TOO_SHORT = -2,

    // The provided buffer is transmitted.
    DMLUDP_ERR_STOP = -3;


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

};

Config* dmludp_config_new(){
    Config* config = new Config();
    return config;
}

void dmludp_config_free(Config* config){
    delete config;
}

int dmludp_header_info(uint8_t* data, size_t buf_len, int ty, int pn) {
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

Connection* dmludp_accept(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return Connection::accept(local, peer, config);
}

Connection* dmludp_connect(sockaddr_storage local, sockaddr_storage peer, Config config) {
    return Connection::connect(local, peer, config);
}

void dmludp_set_rtt(Connection* conn, long interval){
    conn->set_rtt(interval);
}

// Write data from application to protocal
void dmludp_data_write(Connection* conn, const uint8_t* buf, size_t len){
    conn->data_write(data_slice, len);
}

// Fill up congestion control window
bool dmludp_conn_send_all(Connection* conn) {
    return conn->send_all();
}

bool dmludp_conn_is_empty(Connection* conn){
    return conn->data_is_empty();
}

long dmludp_get_rtt(Connection* conn){
    return conn->get_rtt();
}


ssize_t dmludp_send_data_stop(Connection* conn, uint8_t* out, size_t out_len){
    if (out_len <= 0 ){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> out_vector(out_len);
    size_t written = conn->send_data_stop(out_vector);
    memcpy(out, out_vector.data(), out_len);
    return static_cast<ssize_t>(written);
}

bool dmludp_buffer_is_empty(Connection* conn){
    return conn->is_empty();
}

bool dmludp_conn_is_stop(Connection* conn){
    return conn->is_stopped();
}

ssize_t dmludp_send_data_handshake(Connection* conn, uint8_t* out, size_t out_len){
    if (out_len <= 0 ){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> out_vector(out_len);
    size_t written = conn->send_data_handshake(out_vector);
    memcpy(out, out_vector.data(), out_len);
    return static_cast<ssize_t>(written);
}

bool dmludp_conn_is_closed(Connection* conn){
    return conn->is_closed();
}

void dmludp_conn_free(Connection* conn) {
    delete conn;
}


ssize_t dmludp_conn_send(Connection* conn, uint8_t* out, size_t out_len) {
    if(out_len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    if conn->is_stopped() {
        return dmludp_error::DMLUDP_ERR_DONE;
    }

    std::vector<uint8_t> buf(out_len);

    size_t written = conn->send_data(buf);
    memcpy(out, buf.data(), out_len);

    return static_cast<ssize_t>(written);
}


ssize_t dmludp_conn_recv(Connection* conn, const uint8_t* buf, size_t buf_len){
    if(out_len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> recv_buf(buf, buf + out_len);

    size_t received = conn->recv_slice(recv_buf);

    return static_cast<ssize_t>(received);

}

ssize_t dmludp_data_read(Connection* conn, uint8_t* buf, size_t len){
    if(out_len <= 0){
        return dmludp_error::DMLUDP_ERR_BUFFER_TOO_SHORT;
    }

    std::vector<uint8_t> data_slice(buf, buf+len);

    size_t result = conn->read(data_slice);

    return static_cast<ssize_t>(result);
}