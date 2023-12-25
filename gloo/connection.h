#pragma once
#include <unordered_map>
#include <cstdint>
#include <sys/socket.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <sys/socket.h>

#include "gloo/packet.h"
#include "gloo/Recovery.h"
#include "gloo/recv_buf.h"
#include "gloo/send_buf.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define IS_BIG_ENDIAN 1
#else
    #define IS_BIG_ENDIAN 0
#endif

namespace dmludp {


const size_t HEADER_LENGTH = 26;

const size_t ELICT_FLAG = 8;
// use crate::ranges;
const double CONGESTION_THREAHOLD = 0.01;

/// The minimum length of Initial packets sent by a client.
const size_t MIN_CLIENT_INITIAL_LEN = 1350;

// The default max_datagram_size used in congestion control.
const size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1350;


struct SendInfo {
    /// The local address the packet should be sent from.
    sockaddr_storage from;

    /// The remote address the packet should be sent to.
    sockaddr_storage to;

};

struct RecvInfo {
    /// The remote address the packet was received from.
    sockaddr_storage from;

    /// The local address the packet was received on.
    sockaddr_storage to;
};

class Config {
    public:
    size_t max_send_udp_payload_size;

    uint64_t max_idle_timeout;

    Config():
    max_send_udp_payload_size(MAX_SEND_UDP_PAYLOAD_SIZE),
    max_idle_timeout(5000){};

    ~Config(){

    };

    /// Sets the `max_idle_timeout` transport parameter, in milliseconds.
    /// same with tcp max idle timeout
    /// The default value is infinite, that is, no timeout is used.
    void set_max_idle_timeout(uint64_t v) {
        max_idle_timeout = v;
    };
};


class Connection{
    public: 
    size_t recv_count;

    /// Total number of sent packets.
    size_t sent_count;

    /// Whether this is a server-side connection.
    bool is_server;

    /// Whether the connection handshake has been completed.
    bool handshake_completed;

    /// Whether the connection handshake has been confirmed.
    bool handshake_confirmed;

    /// Whether the connection is closed.
    bool closed;

    // Whether the connection was timed out
    bool timed_out;

    bool server;

    struct sockaddr_storage localaddr;

    struct sockaddr_storage peeraddr;
    
    size_t written_data;

    bool stop_flag;

    bool stop_ack;

    std::unordered_map<uint64_t, uint8_t> prioritydic;

    std::vector<uint64_t> sent_pkt;

    std::unordered_map<uint64_t, uint8_t> recv_dic;

    //store data
    std::vector<uint8_t> send_data_buf;

    //store norm2 for every 256 bits float
    // Note: It refers to the priorty of each packet.
    std::vector<uint8_t> norm2_vec;

    size_t record_win;

    uint64_t total_offset;

    bool recv_flag;

    std::unordered_map<uint64_t, uint64_t> recv_hashmap;

    bool feed_back;

    size_t ack_point;

    uint64_t max_off;

    uint64_t send_num;

    size_t high_priority;

    size_t sent_number;

    std::unordered_map<uint64_t, uint64_t> sent_dic;

    bool bidirect;

    Recovery recovery;

    std::array<PktNumSpace, 2> pkt_num_spaces;

    std::chrono::nanoseconds rtt;
    
    std::chrono::high_resolution_clock::time_point handshake;

    SendBuf send_buffer;

    RecvBuf rec_buffer;
 
    // static Connection* connect(sockaddr_storage local, sockaddr_storage peer, Config config ) {
    static std::shared_ptr<Connection> connect(sockaddr_storage local, sockaddr_storage peer, Config config ) {
        // auto conn = new Connection(local, peer, config, false);
        // return conn;
        return std::make_shared<Connection>(local, peer, config, false);
    };

    // static Connection* accept(sockaddr_storage local, sockaddr_storage peer, Config config)  {
    static std::shared_ptr<Connection> accept(sockaddr_storage local, sockaddr_storage peer, Config config)  {
        // auto conn = new Connection(local, peer, config, true);
        // return conn;
        return std::make_shared<Connection>(local, peer, config, true);

    };

    Connection(sockaddr_storage local, 
    sockaddr_storage peer, 
    Config config,
    bool server):    
    recv_count(0),
    sent_count(0),
    is_server(server),
    handshake_completed(false),
    handshake_confirmed(false),
    closed(false),
    timed_out(false),
    server(server),
    localaddr(local),
    peeraddr(peer),
    written_data(0),
    stop_flag(false),
    stop_ack(false),
    // std::unordered_map<uint64_t, uint8_t> prioritydic;
    // std::vector sent_pkt: Vec<u64>;
    // std::unordered_map<uint64_t, uint8_t> recv_dic;
    // std::vector send_data_buf:Vec<u8>;
    // std::vector norm2_vec:Vec<u8>;
    prioritydic(),
    sent_pkt(),
    recv_dic(),
    send_data_buf(),
    norm2_vec(),
    record_win(0),
    total_offset(0),
    recv_flag(false),
    // std::unordered_map<uint64_t, uint64_t> recv_hashmap;
    feed_back(false),
    ack_point(0),
    max_off(0),
    send_num(0),
    high_priority(0),
    sent_number(0),
    rtt(0),
    handshake(std::chrono::high_resolution_clock::now()),
    // std::unordered_map<uint64_t, uint64_t> sent_dic;
    bidirect(true)
    // Recovery recovery,
    // SendBuf send_buffer,
    // RecvBuf recv_buffer,
    {};

    ~Connection(){

    };

    void update_rtt(){
        auto arrive_time = std::chrono::high_resolution_clock::now();
        if (rtt.count() == 0 ){
            rtt = arrive_time - handshake;
        }else{
            rtt = 17 * ( arrive_time - handshake ) / 16;
        }
    };

    void set_rtt(uint64_t inter){
        if (bidirect){
            rtt = std::chrono::nanoseconds(inter);
        }
    };

    void new_rtt(uint64_t last){
        rtt = rtt / 4 + 3 * std::chrono::nanoseconds(last)/4;
    };

    uint64_t convertToUint64(const std::vector<uint8_t>& v){
        uint64_t result = 0;
        for (int i = 0; i < 8; ++i) {
        #if  IS_BIG_ENDIAN
            result |= static_cast<uint64_t>(v[i]) << (56 - i * 8);
        #else
            result |= static_cast<uint64_t>(v[i]) << i * 8;
        #endif
        }
        return result;
    };

    size_t recv_slice(std::vector<uint8_t> buf){
        auto len = buf.size();

        // see in dmludp.h
        // if (len == 0){
        //     return Err(Error::BufferTooShort);
        // }

        recv_count += 1;

        auto hdr = Header::from_slice(buf);

        size_t read = 0;

        if (hdr->ty == Type::Handshake && is_server){
            update_rtt();
            handshake_completed = true;
        }
        
        //If receiver receives a Handshake packet, it will be papred to send a Handshank.
        if (hdr->ty == Type::Handshake && !is_server){
            handshake_confirmed = false;
            feed_back = true;
        }
        
        // All side can send data.
        if (hdr->ty == Type::ACK){
            process_ack(buf);
        }

        if (hdr->ty == Type::ElictAck){
            recv_flag = true;
            std::vector<uint8_t> subbuf( buf.begin() + 1, buf.begin()+ 1 + sizeof(uint64_t));
            send_num = convertToUint64(subbuf);
            std::vector<uint8_t> checkbuf( buf.begin() + 26, buf.end());
            check_loss(checkbuf);
            feed_back = true;
        }

        if (hdr->ty == Type::Application){
            recv_count += 1;
            read = (size_t)(hdr->pkt_length);
            std::vector<uint8_t> writebuf(buf.begin() + 26, buf.end());
            rec_buffer.write(writebuf, hdr->offset);
            recv_dic.insert(std::make_pair(hdr->offset, hdr->priority));
        }

        // In dmludp.h
        // if (hdr->ty == packet::Type::Stop){
        //     return Err(Error::Stopped);
        // }

        // if (hdr->ty == packet::Type::Fin){
        //     return Err(Error::Done);
        // }

        return read;
    };

    bool send_ack(){
        return feed_back;
    };


    
    //Get unack offset. 
    void process_ack(std::vector<uint8_t> buf){
        std::vector<uint8_t> unackbuf(buf.begin() + 26, buf.end());

        std::vector<uint8_t> ackvector(unackbuf.begin(), unackbuf.begin()+8);

        uint64_t max_ack = convertToUint64(ackvector);

        if (max_ack > max_off){
            max_off = max_ack;
        }

        size_t len = unackbuf.size();
        size_t start = 8;
        float weights = 0;
        while (start < len){
            std::copy(unackbuf.begin() + start, unackbuf.begin() + start + 8, ackvector.begin());
            uint64_t unack = convertToUint64(ackvector);
            start += 8;
            std::copy(unackbuf.begin() + start, unackbuf.begin() + start + 8, ackvector.begin());
            uint64_t priority = convertToUint64(ackvector);

            if ( sent_dic.find(unack) != sent_dic.end()){
                if (sent_dic.at(unack) == 0){
                    send_buffer.ack_and_drop(unack);
                }else{
                    continue;
                }
            }else{
                continue;
            }

            if (priority != 0){
                // convert the result of priority_calculation to uint64
                priority = priority_calculation(unack);
            }
            start += 8;
            if (priority == 1){
                weights += 0.15;
            }else if (priority == 2) {
                weights += 0.2;
            }else if (priority == 3) {
                weights += 0.25;
            }else{
                send_buffer.ack_and_drop(unack);
            }
            auto real_priority = priority_calculation(unack);
            if (priority != 0 && real_priority == 3){
                high_priority += 1;
            }
        }

        recovery.update_win(weights, (double)(len/(8*2)));
        
    };

    uint8_t findweight(uint64_t unack){
        return prioritydic.at(unack);
    };

    bool send_all(){
        stop_flag = false;
        stop_ack = false;
        
        // need to change!!!!!!!!!
        set_handshake();

        if (send_data_buf.size() > 0){
            auto written = write();
            send_data_buf.erase(send_data_buf.begin() , send_data_buf.begin() + written);
            written_data += written;
            total_offset += (uint64_t)written;
            return true;
        }else {
            if (send_buffer.data.empty()){
                return false;
            }else{
                // continue send
                auto written = write();
                send_data_buf.erase(send_data_buf.begin() , send_data_buf.begin() + written);
                written_data += written;
                total_offset += (uint64_t)written;
                return true;
            }
        }
        
    };

    void addUint64 (std::vector<uint8_t>& v, uint64_t input){
        #if  IS_BIG_ENDIAN
        for (size_t i = 0; i < sizeof(uint64_t); ++i) {
            v.push_back(static_cast<uint8_t>(input >> (i * 8)));
        }
        #else
        for (int i = sizeof(uint64_t) - 1; i >= 0; --i) {
            v.push_back(static_cast<uint8_t>(input >> (i * 8)));
        }
        #endif
    };

    //Send single packet
    ///////
    size_t send_data(std::vector<uint8_t> &out){
        
        size_t done = 0;
        size_t total_len = HEADER_LENGTH;

        uint64_t pn = 0;
        uint64_t offset = 0;
        uint8_t priority = 0;
        uint64_t psize = 0;

        auto ty = write_pkt_type(); 

        Header* hdr = new Header(Type::Application, 0, 0, 0, 0);

        if (ty == Type::Handshake && server){
            hdr->ty = ty;
            hdr->pkt_num = pn;
            hdr->offset = offset;
            hdr->priority = priority;
            hdr->pkt_length = psize;
            hdr->to_bytes(out);
            set_handshake();
        }

        if (ty == Type::Handshake && !server){
            hdr->ty = ty;
            hdr->pkt_num = pn;
            hdr->offset = offset;
            hdr->priority = priority;
            hdr->pkt_length = psize;
            hdr->to_bytes(out);
            feed_back = false;
        }
    
        //send the received packet condtion
        if (ty == Type::ACK){
            feed_back = false;
            psize = (uint64_t)(recv_hashmap.size()*8*2 + 8);
            hdr->ty = ty;
            hdr->pkt_num = send_num;
            hdr->offset = 0;
            hdr->priority = 0;
            hdr->pkt_length = psize;
            hdr->to_bytes(out);

            // pkt_length may not be 8
            size_t off = 26;
            for (const auto& pair : recv_hashmap) {
                addUint64(out, pair.first);
                addUint64(out, pair.second);
            }
            recv_hashmap.clear();
        }

        // chekc is_ack condition is correct or not.
        if (ty == Type::ElictAck){
            // pn =  pkt_num_spaces[1].next_pkt_num;
            // pkt_num_spaces[1].next_pkt_num += 1;
            pn = pkt_num_spaces.at(1).updatepktnum();
            if (stop_flag == true){
                std::vector<uint64_t> res(sent_pkt.begin()+ack_point, sent_pkt.end());
                size_t pkt_counter = sent_pkt.size() - ack_point;
                hdr->ty = ty;
                hdr->pkt_num = pn;
                hdr->offset = offset;
                hdr->priority = priority;
                hdr->pkt_length = (uint64_t)(pkt_counter*8);
                hdr->to_bytes(out);

                for (auto it = res.begin(); it != res.end(); ++it) {
                    addUint64(out, *it);
                }
                psize = (uint64_t)(pkt_counter*8);
                ack_point = sent_pkt.size();
                stop_ack = true;
            }
            else{
                std::vector<uint64_t> res(sent_pkt.begin()+ack_point, sent_pkt.end());
                hdr->ty = ty;
                hdr->pkt_num = pn;
                hdr->offset = offset;
                hdr->priority = priority;
                hdr->pkt_length = 64;
                hdr->to_bytes(out);
                for (auto it = res.begin(); it != res.end(); ++it) {
                    addUint64(out, *it);
                }
                ack_point = sent_pkt.size();
                psize = 64;
            }
            total_len += (size_t)psize;
            delete hdr; 
            hdr = nullptr; 

            return total_len;
        }
        
        ////////////////////////////
        if (ty == Type::Application){
            size_t out_len = 0; 
            uint64_t out_off = 0;

            std::vector<uint8_t> data_slice(out.begin()+ 26 , out.end());
            bool stop = send_buffer.emit(data_slice, out_len, out_off);

            sent_count += 1;
            sent_number += 1;
            // pn = pkt_num_spaces[0].next_pkt_num;
            // pkt_num_spaces[0].next_pkt_num += 1;
            pn = pkt_num_spaces.at(0).updatepktnum();
            priority = priority_calculation(out_off);

            hdr->ty = ty;
            hdr->pkt_num = pn;
            hdr->offset = out_off;
            hdr->priority = priority;
            hdr->pkt_length = (uint64_t)out_len;

            offset = (uint64_t)out_len;
            psize = (uint64_t)out_len;
            hdr->to_bytes(out);
                
            if (stop == true){
                stop_flag = true;
            }

            if (sent_dic.find(out_off) != sent_dic.end()){
                sent_dic[out_off] -= 1;
            }else{
                sent_dic[out_off] = priority;
            }

            sent_pkt.push_back(hdr->offset);
        }  

        if (ty == Type::Stop){
            hdr->ty = ty;
            hdr->pkt_num = pn;
            hdr->offset = offset;
            hdr->priority = priority;
            hdr->pkt_length = psize;

            hdr->to_bytes(out);
            
            total_len += (size_t)psize;

            delete hdr; 
            hdr = nullptr; 
            return total_len;
        }

        handshake = std::chrono::high_resolution_clock::now();

        total_len += (size_t)psize;

        delete hdr; 
        hdr = nullptr; 
        return total_len;
    };

    size_t send_data_stop(std::vector<uint8_t> &out){ 
        size_t total_len = HEADER_LENGTH;

        // auto pn =  pkt_num_spaces[1].next_pkt_num;
        // pkt_num_spaces[1].next_pkt_num += 1;

        auto pn = pkt_num_spaces.at(1).updatepktnum();

        uint64_t offset = 0;
        uint8_t priority = 0;
        uint64_t psize = 0;

        auto ty = Type::Stop; 

        Header* hdr = new Header(ty, pn, offset, priority, psize);

        hdr->to_bytes(out);
        delete hdr; 
        hdr = nullptr; 

        return total_len;
    };

    size_t send_data_handshake(std::vector<uint8_t> &out){     
        size_t total_len = HEADER_LENGTH;

        uint64_t pn = 0;
        uint64_t offset = 0;
        uint8_t priority = 0;
        uint64_t psize = 0;

        auto ty = Type::Handshake; 

        Header* hdr = new Header(ty, pn, offset, priority, psize);

        hdr->to_bytes(out);

        delete hdr; 
        hdr = nullptr; 

        return total_len;
    };

    bool is_stopped(){
        return stop_flag && stop_ack;
    };

    //Start updating congestion control window and sending new data.
    bool is_ack(){
        auto now = std::chrono::high_resolution_clock::now();
        auto interval = now - handshake;
        if (interval > rtt){
            return true;
        }
        return false;
    };

///////////////////
    size_t read(std::vector<uint8_t> out){
        return rec_buffer.emit(out);
    };

    uint64_t max_ack() {
        return rec_buffer.max_ack();
    };
  
    //Writing data to send buffer.
    size_t write() {
        if (send_buffer.data.empty()){
            auto toffset = total_offset % 1024;
            size_t off_len = 0;
            if (toffset % 1024 != 0){
                off_len = (size_t)(1024 - (total_offset % 1024));
            }
            auto high_ratio = (double)high_priority  / (double)sent_number;
            high_priority = 0;
            sent_number = 0;
            // Note: written_data refers to the non-retransmitted data.

            size_t congestion_window = 0;
            if (high_ratio > CONGESTION_THREAHOLD){
                congestion_window = recovery.rollback();
            }else{
                congestion_window = recovery.cwnd();
            };
            record_win = congestion_window;
            auto result = send_buffer.write(send_data_buf, congestion_window, off_len, max_off);
            return result;
        }else{
            auto congestion_window = record_win;
            size_t off_len = 0;

            auto result = send_buffer.write(send_data_buf, congestion_window, off_len, max_off);
            return result;
        }

    };

    uint8_t priority_calculation(uint64_t off){
        auto real_index = (uint64_t)(off/1024);
        return norm2_vec[real_index];
    };

    void reset(){
        norm2_vec.clear();
        send_buffer.clear();
    };

//////////////////////////
    void check_loss(std::vector<uint8_t> b){
        // auto b = octets::OctetsMut::with_slice(recv_buf);

        // let result:Vec<u64> = Vec::new();
        int start = 0;
        while (b.size()>0) {
            auto offset = Header::get_u64(b, start);
            start += sizeof(uint64_t);
            // let offset = b.get_u64().unwrap();
            if (recv_dic.find(offset)!= recv_dic.end()){
                recv_hashmap.insert(std::make_pair(offset, 0));
            }else{
                recv_hashmap.insert(std::make_pair(offset, 1));
            }
        }
    };

    void set_handshake(){
        handshake = std::chrono::high_resolution_clock::now();
    };

    double get_rtt() {
        return rtt.count();
    };

    /// Returns the maximum possible size of egress UDP payloads.
    ///
    /// This is the maximum size of UDP payloads that can be sent, and depends
    /// on both the configured maximum send payload size of the local endpoint
    /// (as configured with [`set_max_send_udp_payload_size()`]), as well as
    /// the transport parameter advertised by the remote peer.
    ///
    /// Note that this value can change during the lifetime of the connection,
    /// but should remain stable across consecutive calls to [`send()`].
    ///
    /// [`set_max_send_udp_payload_size()`]:
    ///     struct.Config.html#method.set_max_send_udp_payload_size
    /// [`send()`]: struct.Connection.html#method.send
    size_t max_send_udp_payload_size() {
        return MIN_CLIENT_INITIAL_LEN;
    };
    

    /// Returns true if the connection handshake is complete.
    bool is_established(){
        return handshake_completed;
    };


    /// Returns true if the connection is closed.
    ///
    /// If this returns true, the connection object can be dropped.
    bool is_closed() {
        return closed;
    };

    /// Returns true if the connection was closed due to the idle timeout.
    bool is_timed_out() {
        return timed_out;
    };
    
    Type write_pkt_type(){
        // let now = Instant::now();
        if (rtt.count() == 0 && is_server == true){
            handshake_completed = true;
            return Type::Handshake;
        }

        if (handshake_confirmed == false && is_server == false){
            handshake_confirmed = true;
            return Type::Handshake;
        }

        if ((sent_count % ELICT_FLAG == 0 && sent_count > 0) || stop_flag == true){
            sent_count = 0;
            return Type::ElictAck;
        }

        if (recv_flag == true){
            recv_flag = false;
            return Type::ACK;
        }

        if (rtt.count() != 0){
            return Type::Application;
        }

        if (send_buffer.data.empty()){
            return Type::Stop;
        }

        return Type::Unknown;

        // Err(Error::Done)
    };

    // Send buffer is empty or not. If it is empty, send_all() will try to fill it with new data.
    bool data_is_empty(){
        return send_buffer.data.empty();
    };

    bool is_empty(){
        return send_data_buf.empty();
    };

    // Application can send data through this function, 
    // It can dynamically add the new coming data to the buffer.
    void data_write(const uint8_t* buf, size_t length){
        if (!norm2_vec.empty()){
            norm2_vec.clear();
        }
        if (!send_data_buf.empty()){
            send_data_buf.clear();
        }

        size_t len = 0;
        if (length % 1024 == 0){
            len = length / 1024;
        }else{
            len = length/1024 + 1;
        }

        send_data_buf.insert(send_data_buf.end(), buf, buf+length);

        norm2_vec.insert(norm2_vec.end(), len, 3);

        send_buffer.clear();
    };
};

}