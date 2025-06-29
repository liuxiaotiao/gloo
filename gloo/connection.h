#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <vector>
#include <algorithm>
#include <chrono>
#include <set>
#include <sys/uio.h>
#include <stdlib.h>
#include <numeric>
#include "packet.h"
#include "cubic.h"
#include "recv_buf.h"
#include "send_bufs.h"
#include "tool.h"
#include <cmath>
#include <typeinfo>
#include <dlfcn.h>
#include <cassert>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <optional>
#include <linux/net_tstamp.h>  // SOF_TIMESTAMPING_* 宏定义
#include <linux/socket.h> 
#include <span>

#define BENCH_START(name) auto __##name##_start = std::chrono::high_resolution_clock::now()
#define BENCH_END(name) \
    do { \
        auto __##name##_end = std::chrono::high_resolution_clock::now(); \
        std::cout << "[BENCH] " #name " took " \
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(__##name##_end - __##name##_start).count() \
                  << " ns\n"; \
    } while(0)

namespace dmludp {


using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Offset_len = uint64_t;

using Difference_len = uint32_t;

using Packet_len = uint16_t;

using Importance_len = uint8_t;

using Important_blocks_len = uint16_t;

using Status_len = uint8_t;

using Slot_len = uint32_t;

constexpr auto RTO_MIN = std::chrono::microseconds(200);

constexpr auto RTO_MAX = std::chrono::microseconds(800);

class Message{
    public:
        struct msghdr message_body;

        iovec iov[2];

        Header message_header;

        Message(){
            iov[0].iov_base = static_cast<void*>(&message_header);
            iov[0].iov_len = sizeof(Header) - 5; /*Don't send padding part*/

            iov[1] = {nullptr, 0};

            memset(&message_body, 0, sizeof(msghdr));
            message_body.msg_iov = iov;
            message_body.msg_iovlen = 2; // Fixed to 2 iovecs
        } 

        ~Message(){};

        void setMessageBody(void* buffer, size_t length) {
            iov[1].iov_base = buffer;
            iov[1].iov_len = length;
        }

        void setMessageHeader(Packet_num_len pn, Offset_len offset, Difference_len difference, 
            Packet_len length, Importance_len importance_, 
            Block_len blocks_ = std::numeric_limits<Block_len>::max(), 
            Status_len status_ = 0, Type_len ty_ = Type::Application) {
            if (ty_ != Type::Application){
                message_header.pkt_ty = ty_;
            }
            message_header.pkt_num = pn;
            message_header.offset = offset;
            message_header.difference = difference;
            message_header.pkt_length = (Packet_num_len)length;
            message_header.pkt_importance = importance_;
            message_header.pkt_important_block = blocks_;
            message_header.pkt_status = status_;
        }

        Packet_num_len get_packet_number(){
            return message_header.get_pkt_num();
        }

        uint8_t get_packet_type(){
            return message_header.get_ty();
        }

        Offset_len get_packet_offset(){
            return message_header.get_offset();
        }

        Difference_len get_packet_difference(){
            return message_header.get_difference();
        }

        uint16_t get_packet_length(){
            return message_header.get_pkt_length();
        }

        Importance_len get_packet_importance(){
            return message_header.get_pkt_importance();
        }

        Block_len get_blocks(){
            return message_header.get_important_blocks();
        }

        Status_len get_connection_status(){
            return message_header.get_transmissionstatus();
        }


        msghdr* getMessageHeader() {
            return &message_body;
        }
};

class RCMessage : public Message {
    private:
        // bool use_status = true;
        char control[CMSG_SPACE(sizeof(timespec) * 3)];
        uint8_t rx_buffer[MAX_SEND_UDP_PAYLOAD_SIZE];
    public:
        RCMessage(){
            message_body.msg_control = control;
            message_body.msg_controllen = sizeof(control);
            memset(rx_buffer, 0, MAX_SEND_UDP_PAYLOAD_SIZE);
            set_receive_message(rx_buffer, MAX_SEND_UDP_PAYLOAD_SIZE);
        }
            
        void set_receive_message(void *ptr, size_t ptr_len){
            iov[1].iov_base = ptr;
            iov[1].iov_len = ptr_len;
        }

        ~RCMessage(){};
};

class MetaInfo{
    public:
        SendBuf metabuf;

        const Difference_len MetaDifference;

        uint64_t difference_flag = LIMIT_UINT64_T;

        size_t range_len;

        size_t block_type = std::numeric_limits<size_t>::max();

        /* true is complete, false is not complete*/
        bool send_status = false;

        MetaInfo(size_t difference_flag_ = std::numeric_limits<Difference_len>::max()): 
        MetaDifference(difference_flag_),
        metabuf(MAX_SEND_UDP_PAYLOAD_SIZE),
        range_len(0){};

        ~MetaInfo(){};

        bool no_overlap(const std::pair<Packet_num_len, Packet_num_len>& p1, const std::pair<Packet_num_len, Packet_num_len>& p2) {
            return p1.second < p2.first || p2.second < p1.first;
        }

        Difference_len get_difference(){
            return difference_flag;
        }

        void set_buffer(struct iovec* iovecs, int iovecs_len, size_t type_, const Difference_len difference_, 
            std::span<uint64_t> priotity_list = {}, uint64_t startbit = 0, uint64_t endbit = 0){
            if (difference_flag != LIMIT_UINT64_T){
                std::cerr << "MetaInfo set_buffer error(difference_flag(" << (int)difference_flag << "), (" << (int)difference_ << "))" << std::endl;
                _Exit(0);
            }
            difference_flag = difference_;
            if ((difference_flag % 16) != MetaDifference){
                std::cerr << "difference_flag(" << (int)difference_flag << "), MetaDifference(" << (int)MetaDifference << ")" << std::endl;
                _Exit(0);
            }
            if (priotity_list != {}){
                metabuf.add_Meta(iovecs, iovecs_len, priotity_list, startbit, startbit);
            } else {
                metabuf.add_Meta(iovecs, iovecs_len);
            }
            range_len = 0;
            block_type = type_;
            for (auto i = 0; i < iovecs_len; i++){
                range_len += (iovecs[i].iov_len + MAX_SEND_UDP_PAYLOAD_SIZE - 1) / MAX_SEND_UDP_PAYLOAD_SIZE;
            }
        }

        bool iscomplete(){
            return metabuf.written_complete() || send_status == true;
        }

        size_t sent(){
            return metabuf.sentComplete();
        }

        void complete(){
            send_status = true;
        }

        void clear(){
            metabuf.clear();
            block_type = std::numeric_limits<size_t>::max();
            difference_flag = LIMIT_UINT64_T;
            range_len = 0;
            send_status = false;
        }

        void MetaInfo_log(bool contain_ = false) const{
            std::cout << "{";
            std::cout << "\"class\": \"MetaInfo\", ";
            std::cout << "\"MetaDifference\": \"" << (int)MetaDifference << "\", ";
            std::cout << "\"block size\": " << range_len << ", ";
            std::cout << "\"block_type\": " << block_type << ", ";
            std::cout << "\"address\": ";
            if (contain_){
                metabuf.to_json();
            }
            std::cout << "}";   
        }
};


class SCircularQueue {
    public:
        std::vector<MetaInfo> data_;
        size_t head_;
        size_t tail_;
        size_t capacity_;
        size_t count_ = 0;

        uint64_t lastest_difference;

        SCircularQueue(size_t capacity = DataBlock) 
            : head_(0), tail_(0), capacity_(capacity), lastest_difference(0)
        {
            data_.reserve(capacity);
            for (auto i = 0; i < capacity; i++){
                data_.emplace_back(i);
            }
        }

        void push_back(struct iovec* iovecs, int iovecs_len, int type_, std::span<uint64_t> bitmapspan = {}, uint64_t startbit = 0, uint64_t endbit = 0) {
            if (full()){
                std::cerr << "SCircularQueue overflow" << std::endl;
                _Exit(0);
            }
            // if (iovecs_len == 1) {
            //     std::cout<<"push_back:"<<lastest_difference<<", "<<iovecs[0].iov_len<<std::endl;
            // } else {
            //     std::cout<<"push_back:"<<lastest_difference<<", "<<(iovecs[0].iov_len + iovecs[1].iov_len)<<std::endl;
            // }
            data_[tail_].set_buffer(iovecs, iovecs_len, type_, lastest_difference, bitmapspan, startbit, endbit);
            lastest_difference++;
            tail_ = (tail_ + 1) % capacity_;
            count_++;
        }

        void pop_front() {
            if (empty()) {
                throw std::runtime_error("Queue is empty, cannot remove element.");
            }
            data_[head_].clear();
            head_ = (head_ + 1) % capacity_;
            count_--;
        }

        size_t size() const {
            return count_;
            if (tail_ >= head_) {
                return tail_ - head_;
            } else {
                return capacity_ - (head_ - tail_);
            }
        }

        bool empty() const {
            return count_ == 0;
        }

        size_t get_count(){
            return count_;
        }

        bool ready() {
            if(empty()){
                return false;
            }
            bool isReady = false;
            size_t start_index = start();
            for (auto i = 0; i < get_count(); i++){
                size_t pos = (start_index + i) % capacity_;
                isReady |= !data_[pos].metabuf.is_empty();
            }
            return isReady;
        }

        bool full() const {
            return count_ == capacity_;
        }

        void clear() {
            head_ = tail_ = 0;
            count_ = 0;
        }

        size_t end(){
            return tail_;
        }

        size_t start(){
            return head_;
        }

        size_t max(){
            return capacity_;
        }

        size_t frontsent(){
            return data_[head_].sent();
        }

        size_t get_capacity(){
            return capacity_;
        }

        Difference_len get_difference(size_t index_){
            return data_[index_].get_difference();
        }


        void pkt2ack(Difference_len difference_, Offset_len offset_, Packet_num_len pkt, 
            Channel channel_,  bool value_){
            auto index = difference_ % get_capacity();
            // auto status1 = data_[index].metabuf.get_status();
            if (channel_ == Channel::Important) {
                data_[index].metabuf.acknowledege_and_drop(offset_, value_);
            } else {
                data_[index].metabuf.acknowledege_and_drop_unimportant(offset_, value_);
            }
            // data_[index].metabuf.acknowledege_and_drop(offset_, value_);
            // auto status2 = data_[index].metabuf.get_status();
            // if (data_[index].metabuf.sentComplete() < 1024 * 1024){
            // std::cout<<difference_<<", "<<pkt<<", "<<offset_<<", "<<value_<<", "<<data_[index].metabuf.initlosscount<<", "<<status1<<", "<<status2<<std::endl;
            // }
                
        }

        void pkt2ack_important(Difference_len difference_, Offset_len offset_, Packet_num_len pkt, 
            bool value_, bool unreliabelStatus_){
            auto index = difference_ % get_capacity();
            data_[index].metabuf.acknowledege_and_drop(offset_, value_, unreliabelStatus_);
        }

        void pkt2ack_unimportant(
            Difference_len difference_, 
            Offset_len offset_, 
            Packet_num_len pkt, 
            Channel channel_,  
            bool ack_value_,
            PktStatus pktstatus_){

            auto index = difference_ % get_capacity();
            data_[index].metabuf.acknowledege_and_drop_unimportant(offset_, ack_value_, pktstatus_);
        }

        size_t get_status(Difference_len difference_){
            auto index = difference_ % get_capacity();           
            return data_[index].metabuf.get_status();
        }

        bool emit(size_t index_, struct iovec& src_, ssize_t &len_, Offset_len &off_){
            return data_[index_].metabuf.emit(src_, len_, off_);
        }

        bool emit_important(size_t index_, struct iovec& src_, ssize_t &len_, Offset_len &off_, 
            Block_len &block_, Status_len &status_, bool & packetType){
            return data_[index_].metabuf.emit(src_, len_, off_);
        }

        bool emit_unimportant(size_t index_, struct iovec& src_, ssize_t &len_, Offset_len &off_,
            Block_len &block_, PktStatus &status_){
            return data_[index_].metabuf.emit_unimportance(src_, len_, off_, block_, status_);
        }

        bool iscomplete(Difference_len difference_){
            auto index_ = difference_ % get_capacity();
            // if (index_ >= get_capacity()){
            //     std::cerr << "bool iscomplete(Difference_len difference_) index out of boundary" << std::endl;
            //     _Exit(0);
            // }
            return data_[index_].iscomplete();
        }

        bool inrangecheck(uint8_t index){
            bool inRange = false;
            if (head_ < tail_) {
                inRange = (index >= head_ && index < tail_);
            } else {
                inRange = (index >= head_ || index < tail_);
            }
            return inRange;
        }

        bool iscomplete_check(Difference_len difference_){
            auto index_ = difference_ % get_capacity();
            if (!inrangecheck(index_)){
                std::cerr << "difference_("<<(int)difference_<<") not in range("<< head_ << ", " << tail_ <<")" << std::endl;
                _Exit(0);
            }
            return data_[index_].iscomplete();
        }

        /*
        0. i < j
        1. i == j
        2. i > j
        */
        size_t compareIndices(int i, int j) const {
            int pos_i = (i + capacity_ - head_) % capacity_;
            int pos_j = (j + capacity_ - head_) % capacity_;
            if (pos_i < pos_j)
                return 0;
            else if (pos_i > pos_j)
                return 2;
            else
                return 1;
        }

        MetaInfo& at(size_t i) {
            if (i >= count_){
                std::cout<<"i:"<<i<<", "<<count_<<std::endl;
                throw std::out_of_range("Index out of range");
            }
            return data_[(head_ + i) % capacity_];
        }

        /*Manully set old difference block as complete*/
        void completecheck(Difference_len difference_){
            for (auto i = 0; i < count_; i++){
                if (at(i).get_difference() < difference_){
                    at(i).complete();
                }else{
                    break;
                }
            }
        }


        ~SCircularQueue() = default; 
};

class metarecebuf{
    public:
        RecvBuf metabuf;

        RCset receive_offset;

        RCset receive_offset_processed;

        std::vector<uint64_t> source_len;

        bool complete_flag = false;

        Difference_len rdifference;

        size_t received = 0;

        size_t processd = 0;

        size_t expected = 0;

        bool has_zero = false;

        bool used = false;

        size_t srcset = 0;

        Slot_len offset0_index = std::numeric_limits<Slot_len>::max();

        size_t important_packet_count = 0; /* Important packet statics */

        std::optional<size_t> expected_important_packets = std::nullopt; /* Real important packet should be*/

        std::optional<uint8_t> unimportant_packets_status = std::nullopt;

        /*
        0: unused, waiting
        1: used, without offset 0
        2: used, has offset 0
        3: used, offset 0 has processed
        4: used, not complete
        5: used, complete
           |---1
        0 -|   |     |---4
           |   |     |   | 
           |---2--3--|   5
                     |   |
                     |----
        */
        size_t status_ = 0;

        metarecebuf(Difference_len difference_ = 0): rdifference(difference_){
            source_len.reserve(2);
            for(auto i = 0; i < 2 ; i++){
                source_len.push_back(0);
            }
        }

        ~metarecebuf(){}

        void clear(){
            receive_offset.clear();
            receive_offset_processed.clear();
            metabuf.reset();
            metabuf.src = nullptr;
            complete_flag = false;
            received = 0;
            has_zero = false;
            srcset = 0;
            processd = 0;
            expected = 0;
            used = false;
            rdifference = 0;
            status_ = 0;
            offset0_index = std::numeric_limits<Slot_len>::max();

            /* Unimportant related */
            important_packet_count = 0;
            expected_important_packets = std::nullopt;
            unimportant_packets_status = std::nullopt;

            for (auto &e:source_len){
                e = 0;
            }
        }

        bool usedcheck(){
            return used;
        }

        /*
        0 unused
        1 used without offset 0
        2 used and have offset 0
        3 offset 0 processed
        4 peeding
        5 completed
        */
        size_t get_status(){
            if (status_ == 3 || status_ == 4){
                completecheck();
            }
            return status_;
        }

        void set_difference(Difference_len difference_){
            rdifference = difference_;
        }

        bool set_start(size_t pos){
            if (offset0_index != std::numeric_limits<Slot_len>::max()){
                return false;
            }
            offset0_index = pos;
            if (status_ == 0 || status_ == 1){
                status_ = 2;
            }
            return true;
        }

        void addexplen(size_t exp_){
            for(auto &e: source_len){
                if (e == 0){
                    expected += exp_;
                    e = exp_;
                    break;
                }
            }
        }

        void set_complete(){
            complete_flag = true;
        }

        bool find(Offset_len pkt_offset, Packet_len pkt_length, Importance_len pkt_importance){
            auto exist = receive_offset.find(pkt_offset);
            if (!exist){
                if (pkt_offset == 0){
                    has_zero = true;
                    if (status_ == 0 || status_ == 1){
                        status_ = 2;
                    }
                }
                receive_offset.insert(pkt_offset);
                metabuf.reg(pkt_length);
                received += pkt_length;
                if (pkt_importance == 1) {
                    ++important_packet_count;
                }
            }
            return exist;
        }

        void set_blocks(Block_len blocks_) {
            if (!expected_important_packets.has_value()) {
                expected_important_packets = blocks_;
            }
        }

        void set_unimportant_status(){
            if (!unimportant_packets_status.has_value()){
                unimportant_packets_status = 1;
            }
        }

        void startcomplete(){
            if (status_ == 2){
                status_ = 3;
            }
        }

        void completecheck(){
            size_t total = 0;
            for(auto const e:source_len){
                total += e;
            }
            status_ = 4;

            if (important_packet_count > expected_important_packets) {
                std::cout<<"[Error] important_packet_count:"<<important_packet_count<<", expected_important_packets:" << expected_important_packets << std::endl;
                _Exit(0);
            }
        
            bool complete_ = unimportant_packets_status.has_value() && expected_important_packets == important_packet_count;

            if (received == total || complete_){
                status_ = 5;
                // std::cout<<"completecheck, received:"<<received<<", "<<total<<std::endl;
            } 
            // std::cout<<"status_:"<<status_<<std::endl;
        }

        bool is_complete(){
            size_t total = 0;
            for(auto const e:source_len){
                total += e;
            }
            if (received == total || complete_flag){
                status_ = 5;
                return true;
            }
            return false;
        }

        bool is_complete2(){
            size_t total = 0;
            for(auto const e:source_len){
                total += e;
            }
            if (total == 0){
                return false;
            }

            if (important_packet_count > expected_important_packets) {
                std::cout<<"[Error] important_packet_count:"<<important_packet_count<<", expected_important_packets:" << expected_important_packets << std::endl;
                _Exit(0);
            }
        
            bool complete_ = unimportant_packets_status.has_value() && expected_important_packets == important_packet_count;

            if (received == total || complete_){
                complete_flag = true;
                return true;
            }
            return false;
        }

        void set_src(uint8_t* src_){
            if(srcset < 2){
                metabuf.src = src_;
                srcset++;
            }
        }

        void processdlen(size_t len_){
            processd += len_;
        }

        void processCheck(){
            /*offset 0 packet didn't received, not do any operation*/
            if (srcset == 0){
                return;
            }
            if (received != processd){
                std::cout<< rdifference << " received:"<<received<<", processd:"<<processd<<std::endl;
                throw std::overflow_error("received != processd");
            }
            return;
        }

        bool processComplete(){
            // std::cout<<"processd:"<<processd<<", "<<expected<<std::endl;
            return processd == expected;
        }

        void copy(size_t offset_, void * src, size_t copy_len){
            if (offset_ == 0){
                status_ = 3;
            }
            if(!metabuf.src){
                std::cout<<"[metarecebuf copy()] "<<(void*)metabuf.src<<std::endl;
                _Exit(0);
            }
            // if (offset_ == 0 && copy_len < 1440)
            memcpy(reinterpret_cast<uint8_t*>(metabuf.src) + offset_, reinterpret_cast<uint8_t*>(src), copy_len);
        }

        bool targetCheck(){
            return metabuf.src != nullptr;
        }

        bool srcsetCheck(){
            return srcset == 1;
        }

        void record_copy(Offset_len offset_){
            auto exist = receive_offset_processed.find(offset_);
            if (!exist){
                receive_offset_processed.insert(offset_);
            }
        }

        bool copyed_check(Offset_len offset_){
            return receive_offset_processed.find(offset_);
        }

        Difference_len get_difference(){
            return rdifference;
        }

        Slot_len get_position(){
            return offset0_index;
        }
};  

/*Receive circular queue*/
class RCircularQueue {
private:
    std::vector<metarecebuf> data_; 
    size_t head_;      
    size_t tail_;      
    size_t capacity_;  
    size_t count_;     
public:
    RCircularQueue(size_t capacity = DataBlock)
        : head_(0), tail_(0), capacity_(capacity), count_(0)
    {
        data_.reserve(capacity_);
        data_.resize(capacity_);
    }

    /*Queue capacity*/
    size_t get_capacity(){
        return capacity_;
    }

    /*Add new data block at the tail*/
    bool push_back(Difference_len difference_) {
        if (count_ == capacity_){
            return false;
        }
        // data_[tail_].clear();
        data_[tail_].set_difference(difference_);
        // std::cout<<"push_back:" << tail_ << ", " << difference_ << std::endl;

        tail_ = (tail_ + 1) % capacity_;

        ++count_;

        return true;
    }

    /*Complete block receive*/
    void pop_front() {
        if (empty()) {
            throw std::runtime_error("Queue is empty, cannot remove element.");
        }
        data_[head_].clear();
        head_ = (head_ + 1) % capacity_;
        --count_;
        // std::cout << "recvCQ pop_front:" << count_ << std::endl;
    }

    size_t size() const {
        return count_;
    }

    bool empty() const {
        return count_ == 0;
    }

    bool full() const {
        return count_ == capacity_;
    }

    void clear() {
        for (size_t i = 0; i < capacity_; i++) {
            data_[i].clear();
        }
        head_ = tail_ = count_ = 0;
    }

    size_t end() const {
        return tail_;
    }

    Difference_len start(){
        return data_[head_].get_difference();
    }

    size_t startpos(){
        return data_[head_].get_position();
    }

    void insert(Difference_len difference, Offset_len pkt_offset, Packet_len pkt_length, uint32_t index_, bool &exist_,
        Importance_len importance_, Block_len &important_blocks, Status_len &status_){
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        exist_ = data_[index].find(pkt_offset, pkt_length, importance_);

        if (important_blocks != std::numeric_limits<Block_len>::max()){
            data_[index].set_blocks(important_blocks);
        }
        if (status_ != 0) {
            data_[index].set_unimportant_status(status_);
        }
    }

    void insert_control_message(Difference_len difference, Block_len important_blocks = std::numeric_limits<Block_len>::max(), Status_len status_ = 0){
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        if (important_blocks != std::numeric_limits<Block_len>::max()){
            data_[index].set_blocks(important_blocks);
        }
        if (status_ != 0) {
            data_[index].set_unimportant_status(status_);
        }
    }

    size_t get_status(Difference_len difference_){
        if (empty()){
            return 0;
        }
        auto index_ = difference_ % get_capacity();
        return data_[index_].get_status();
    }

    /* Complete check */
    bool iscomplete(Difference_len difference) {
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        return data_[index].is_complete();
    }

    void startcomplete(Difference_len difference){
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        return data_[index].startcomplete();
    }

    void completecheck(Difference_len difference){
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        return data_[index].completecheck();
    }

    /*Set target pointer*/
    void set_recv_pointer(Difference_len difference, uint8_t* src) {
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        data_[index].set_src(src);
    }

    /*Set rx_len*/
    void rx_len(Difference_len difference, size_t expected) {
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        // data_[index].addexplen(expected);
    }

    void setRead(Difference_len difference, size_t expected) {
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        if(expected > 48){
            data_[index].addexplen(48);
            data_[index].addexplen(expected - 48);
        }else{
            data_[index].addexplen(48);
        }
    }

    /*Same function as iscomplete, delete later*/
    bool isreceived(Difference_len difference, size_t expected){
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        return data_[index].is_complete();
    }

    /**/
    bool differencecheck(Difference_len difference_){
        if (empty()){
            return true;
        }else{
            auto index = difference_ % get_capacity();
            if (data_[index].usedcheck()){
                return false;
            }
            return true;
        }
    }

    size_t receivestatus(Difference_len difference_){
        auto index_ = difference_ % get_capacity();
        return data_[index_].get_status();
    }


    bool insertzero(Difference_len difference_, Slot_len position_){
        auto index_ = difference_ % get_capacity();
        inrangecheck(index_, __func__);
        return data_[index_].set_start(position_);
    }


    void indexcheck(Difference_len difference_) {
        auto index = difference_ % get_capacity();
        
        bool inRange = false;
        if(!empty()){
             if (head_ < tail_) {
                // Non-wrapping case: the valid range is [head_, tail_)
                inRange = (index >= head_ && index < tail_);
            } else {
                // Wrapping case: the valid range is [head_, capacity_) ∪ [0, tail_)
                inRange = (index >= head_ || index < tail_);
            }
        }

        // std::cout << "inRange:" << inRange << std::endl;
        
        if (!inRange) {
            size_t desiredTail = (index + 1) % capacity_;

            int pushes = (desiredTail + capacity_ - tail_) % capacity_;

            auto advance_relative = [](auto value, auto relative_step) {
                using T = decltype(value);
                using SignedT = std::make_signed_t<T>;
                static_assert(std::is_integral<T>::value, "T must be an integer type"); 
                assert(relative_step <= std::numeric_limits<SignedT>::max());          

                if constexpr (std::is_unsigned<T>::value) {
                    return static_cast<T>(static_cast<SignedT>(value) + relative_step);
                } else {
                    return value + relative_step;
                }
            };

            Difference_len start = advance_relative(difference_, -(pushes-1));

            for (size_t i = 0; i < pushes; ++i) {
                push_back(start + i);
                if (i == (pushes - 1)){
                    if (start + i != difference_){
                        std::cerr << "[indexcheck] start:" << start << ", pushes:" << pushes << ", difference_:" << difference_ << std::endl;
                        _Exit(0);
                    }
                }
            }
        }
    }

    /*Check data block has been registerred in queue*/
    bool inrangecheck(uint8_t index, const char* caller) {
        bool inRange = false;
        // std::cout << "index:" << (int)index << ", " << head_ << ", " << head_ << std::endl;
        if (head_ < tail_) {
            inRange = (index >= head_ && index < tail_);
        } else {
            inRange = (index >= head_ || index < tail_);
        }
        
        if (!inRange) {
            if (caller == "insert"){    
                // std::cout << "index:" << (int)index << ", " << head_ << ", " << head_ << std::endl;
                // std::cerr << "Function '" << caller << "' called inrangecheck, but the result is false." << std::endl;  
                return inRange;
            }
            std::cout << "index:" << (int)index << ", " << head_ << ", " << head_ << std::endl;
            std::cerr << "Function '" << caller << "' called inrangecheck, but the result is false." << std::endl;       
            _Exit(0);
        }

        return inRange;
    }

    /*Check receive data and copy data are same*/
    bool processComplete(Difference_len difference_){
        auto index = difference_ % capacity_;
        return data_[index].processComplete();
    }

    /*Copy data*/
    void copy (Difference_len difference_, Offset_len offset_, void * src_, size_t len_){
        auto index = difference_ % capacity_;
        // std::cout<<"copy:"<<offset_<<std::endl;
        inrangecheck(index, __func__);
        // if (offset_ > 48){
        //     std::cout<<"copy:"<<std::endl;
        //     log_print(src_, len_);
        // }
        data_[index].copy(offset_, src_, len_);
        data_[index].processdlen(len_);
    }

    void record_copy(Difference_len difference_, Offset_len offset_){
        auto index = difference_ % capacity_;
        inrangecheck(index, __func__);
        data_[index].record_copy(offset_);
    }
    

    bool copyed_check(Difference_len difference_, Offset_len offset_){
        auto index = difference_ % capacity_;
        return data_[index].copyed_check(offset_);
    }

    /*Check difference_ block is received*/
    void processCheck(Difference_len difference_){
        auto index = difference_ % capacity_;
        inrangecheck(index, __func__);
        data_[index].processCheck();
    }

    /*Check data pointer is available*/
    bool targetCheck(Difference_len difference_){
        auto index = difference_ % capacity_;
        inrangecheck(index, __func__);
        return data_[index].targetCheck();
    }

    /*Check OP is available*/
    bool srcsetcheck(Difference_len difference_){
        auto index = difference_ % capacity_;
        inrangecheck(index, __func__);
        return data_[index].srcsetCheck();
    }

    metarecebuf& at(size_t i) {
        if (i >= count_)
            throw std::out_of_range("Index out of range");
        return data_[(head_ + i) % capacity_];
    }

    void receive_log(){
        if (count_ == 0){
            return;
        }
        std::cout<<"receive condition"<<std::endl;
        for (auto i = 0; i < count_; i++){
            std::cout << "" << at(i).get_difference() << ", " << at(i).receive_offset.count() << ", " << at(i).get_status()<< std::endl;
        }
    }

     void receive_log(sockaddr_storage peeraddr){
        if (count_ == 0){
            return;
        }
        ip_print(peeraddr);
        std::cout<<"receive condition"<<std::endl;
        for (auto i = 0; i < count_; i++){
            std::cout << "" << at(i).get_difference() << ", " << at(i).receive_offset.count() << ", " << at(i).get_status()<< std::endl;
        }
    }


    ~RCircularQueue() = default;
};

class Connection{
public: 
    /// Whether this is a server-side connection.
    bool is_server;

    /// Whether the connection handshake has been completed.
    bool handshake_completed;

    /// Whether the connection handshake has been confirmed.
    bool handshake_confirmed;

    /// Whether the connection is closed.
    bool closed;

    bool server;

    struct sockaddr_storage localaddr;

    struct sockaddr_storage peeraddr;
    
    bool stop_flag;

    bool stop_ack;

    // map for received application pktnum and corresponding offset
    std::vector<uint8_t> receivevector;

    std::vector<uint8_t> acknowldge_header;

    struct msghdr acknowldge_msghdr;

    std::vector<struct iovec> acknowldge_iov;  

    size_t send_packet_type = 0;

    /*Acknowledge packet number*/
    uint64_t send_num;

    bool bidirect;

    Recovery recovery;

    Recovery low_recovery;

    PktNumSpace pkt_num_spaces;

    std::chrono::nanoseconds rtt;

    std::chrono::nanoseconds srtt;

    std::chrono::nanoseconds minrtt;

    std::chrono::nanoseconds rto;

    std::chrono::nanoseconds rttvar;
    
    std::chrono::high_resolution_clock::time_point handshake;

    bool initial;

    // Record errno
    size_t dmludp_error;

    // Used to record how many packet has been sent before EAGAIN
    size_t dmludp_error_sent;

    static std::shared_ptr<Connection> connect(sockaddr_storage local, sockaddr_storage peer) {
        return std::make_shared<Connection>(local, peer, false);
    };

    static std::shared_ptr<Connection> accept(sockaddr_storage local, sockaddr_storage peer)  {
        return std::make_shared<Connection>(local, peer, true);
    };

    const uint8_t handshake_header[sizeof(Header) - 5] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    const uint8_t fin_header[sizeof(Header) - 5] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // when get new data flow, send_connection_difference++
    // WILL BE DROPPED
    Difference_len send_connection_difference;

    /*
    receive_connection_difference keeps current data flow send_connection_difference. 
    If receive_complete() is true, receive_connection_difference++ to keep track with send_connection_difference.
    WILL BE DROPPEP 
    */
    Difference_len receive_connection_difference;

    Difference_len receive_connection_difference_registration;

    Difference_len receive_connection_difference_acknowlege;

    size_t current_loop_min;

    size_t current_loop_max;

    size_t send_status_flag;

    /*
    ts_record is used to calculate approximate send ts for each packet.
    Approximate ts ~= (2nd ts - 1st ts)/(2nd pkt - 1st pkt + 1)
    */ 
    std::chrono::high_resolution_clock::time_point start_ts;

    std::chrono::high_resolution_clock::time_point end_ts;
    
    /* Replace the conbination of send_msg, send_iov and send_header to reduce packet genaratio cost*/
    std::vector<Message> send_message;

    std::vector<RCMessage> receive_message;
    
    ssize_t start_index = -1;

    ssize_t end_index = -1;

    uint64_t ACKrange;

    size_t max_received = std::numeric_limits<size_t>::max();

    size_t receive_upper_bound = 0;

    size_t receive_upper_limit = 0;

    size_t receive_upper_check = 0;

    std::vector<uint8_t> receive_slot;

    TSCircularQueue tsInfo;

    SCircularQueue sendbufferqueue;

    RCircularQueue recvCQ;

    RecordInfo receive_record;

    /*Avoid multiple cwnd reduction in same tramsmission round*/
    bool first_loss;

    Packet_num_len max_acknowleged = LIMIT_UINT64_T;

    bool rtt_initial = true;

    size_t slot_index = 0;

    PacketMapRingBuffer connection_map;

    std::vector<uint64_t> bitmap_vector;

    Connection(sockaddr_storage local, sockaddr_storage peer, bool server):    
    is_server(server),
    handshake_completed(false),
    handshake_confirmed(false),
    closed(false),
    server(server),
    localaddr(local),
    peeraddr(peer),
    stop_flag(true),
    stop_ack(true),
    send_num(0),
    rtt(0),
    srtt(0),
    minrtt(0),
    rto(0),
    rttvar(0),
    handshake(std::chrono::high_resolution_clock::now()),
    bidirect(true),
    initial(false),
    dmludp_error(0),
    dmludp_error_sent(0),
    send_connection_difference(0),
    receive_connection_difference(0),
    receive_connection_difference_registration(LIMIT_UINT32_T),
    receive_connection_difference_acknowlege(LIMIT_UINT32_T),
    current_loop_min(0),
    current_loop_max(0),
    recovery(MAX_SEND_UDP_PAYLOAD_SIZE),
    low_recovery(MAX_SEND_UDP_PAYLOAD_SIZE),
    send_status_flag(0),
    acknowldge_iov(3, {nullptr, 0}),
    receivevector(MAX_ACK_UDP_PAYLOAD_SIZE, 0),
    receive_slot(RX_CONST, 0),
    connection_map(MAP_CONST),
    first_loss(false)
    {
        memset(&acknowldge_msghdr, 0, sizeof(acknowldge_msghdr));
        acknowldge_msghdr.msg_iov = nullptr;
        acknowldge_msghdr.msg_iovlen = 0;

        send_message.resize(ONCE_LIMIT);

        receive_message.resize(RX_CONST);

        acknowldge_header.resize(sizeof(Header));

        bitmap_vector.resize(24);
        for (auto i = 0; i < bitmap_vector; i++){
            e = 24 - i;
        }
    };

    ~Connection(){};

    void loss_reset(){
        first_loss = false;
    }


    void initial_rtt() {
        auto arrive_time = std::chrono::high_resolution_clock::now();
        srtt = arrive_time - handshake;
        rttvar = srtt / 2;
        rto = srtt + 4 * rttvar;
        if (rto < RTO_MIN) rto = RTO_MIN;
        if (rto > RTO_MAX) rto = RTO_MAX;
    }

    /*
        1st RTO: SRTT <- R， RTTVAR <- R/2， RTO <- SRTT + max (G, K*RTTVAR)， where K = 4.

        RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
        SRTT <- (1 - alpha) * SRTT + alpha * R'
        RTO <- SRTT + max (G, K*RTTVAR)
    */
    void update_rtt(std::chrono::high_resolution_clock::time_point send_time, std::chrono::high_resolution_clock::time_point receive_time, std::chrono::high_resolution_clock::time_point receive_time2 = std::chrono::high_resolution_clock::time_point{}){
        if (rtt_initial){
            minrtt = rtt = srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);
            rttvar = srtt / 2;
            rto = srtt + 4 * rttvar;
            rtt_initial = false;
        }else{
            rtt = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);
            if (rtt < minrtt){
                minrtt = rtt;
            }
            auto tmp_srtt = std::chrono::duration<double, std::nano>(srtt.count() * alpha + (1 - alpha) * rtt.count());
            srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_srtt);
            auto diff = srtt - rtt;
            auto tmp_rttvar = std::chrono::duration<double, std::nano>((1 - beta) * rttvar.count() + beta * std::abs(diff.count()));
            rttvar = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_rttvar);
            rto = srtt + 4 * rttvar;
            if (rto < RTO_MIN) rto = RTO_MIN;
            if (rto > RTO_MAX) rto = RTO_MAX;
        }    
    }

    std::chrono::nanoseconds get_rto(){
        return rto;
    }


    // Merge to intial_rtt
    void set_rtt(uint64_t inter){
        if (bidirect){
            rtt = std::chrono::nanoseconds(inter);
        }
        if (srtt.count() == 0){
            srtt = rtt;
            minrtt = rtt;
        }
        if (rttvar.count() == 0){
            rttvar = rtt / 2;
        }
    };

    // Check timeout or not
    bool on_timeout(){
        bool timeout_;
        std::chrono::nanoseconds duration((uint64_t)(get_rtt()));
        std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        if (handshake + duration < now){
            timeout_ = true;
        }else{
            timeout_ = false;
        }
        return timeout_;
    }

    Type header_info(uint8_t* src, size_t src_len, Packet_num_len &pkt_num, 
        Offset_len &pkt_offset, 
        Difference_len &pkt_difference, Packet_len &pkt_len){
        auto pkt_ty = reinterpret_cast<Header *>(src)->ty;
        pkt_num = reinterpret_cast<Header *>(src)->pkt_num;
        pkt_offset = reinterpret_cast<Header *>(src)->offset;
        pkt_difference = reinterpret_cast<Header *>(src)->difference;
        pkt_len = reinterpret_cast<Header *>(src)->pkt_length;
        return pkt_ty;
    };

    /*Process control message and application packet*/
    bool recv_slice2(size_t rx_count, size_t receive_max_index, struct timespec ts = {0, 0}){
        receive_upper_bound = rx_count;
        receive_upper_limit = std::max(receive_upper_limit, receive_max_index + 1);
        bool send_flag_ = false;
        bool isfirst = true;
        // auto startts = std::chrono::high_resolution_clock::now();
        for (auto i = 0 ; i <= receive_max_index; i++){
            if (receive_slot[i] == 1)
            {
                continue;
            }
            auto pkt_ty = receive_message[i].get_packet_type();
     
            if (pkt_ty == Type::ACK){
                process_acknowledge(i);
            }

            if (pkt_ty == Type::Application){
                process_application_packet(i, isfirst);
              
                send_packet_type = Type::ACK;
                send_flag_ = true;
                isfirst = false;
            }

            if (pkt_ty == Type::ElicitAck) {
                process_elicit_packet(i);
                send_packet_type = Type::ACK;
                send_flag_ = true;
            }

            if (pkt_ty == Type::Stop){
                stop_flag = false;
                send_packet_type = Type::Stop;
                send_flag_ = false;
            }
        }
        return send_flag_;
    }

    bool recv_slice3(size_t index_){
        bool send_flag_ = false;
        auto pkt_ty = receive_message[index_].get_packet_type();
        if (pkt_ty == Type::ACK){
            process_acknowledge(index_);
        }

        if (pkt_ty == Type::Application) {
            process_application2(index_);
            send_packet_type = Type::ACK;
            send_flag_ = true;
        }

        return send_flag_;
    }

    void process_elicit_packet(size_t index){
        auto& msg = receive_message[index];
        Packet_num_len pkt_num = msg.get_packet_number();
        Offset_len pkt_offset = msg.get_packet_offset();
        Difference_len pkt_difference = msg.get_packet_difference();
        auto pkt_length = msg.get_packet_length();
        auto pkt_importance = msg.get_packet_importance();
                
        auto pkt_status = msg.get_connection_status();

        receive_slot[index] = 0;

        if (pkt_num < current_loop_min){
            return;
        }

        if (max_received == std::numeric_limits<size_t>::max() || pkt_num > max_received){
            max_received = pkt_num;
            /* bit map substitude byte map*/
            send_num = pkt_num;
        }  

        size_t pos = pkt_num - current_loop_min;
  
        if(pos > 8000){
            std::memset(receivevector.data(), 0, receivevector.size());
            current_loop_min = pkt_num;
            pos = 0;
        }
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range. (byte_index:"<< byte_index <<", "<< receivevector.size() 
            <<", "<<max_received<<", "<< current_loop_min <<")" << std::endl;
            _Exit(0);
        }

        if (pkt_difference >= receive_connection_difference){
            if (pkt_status != 0){
                recvCQ.insert_control_message(pkt_importance, pkt_status);
            } 
        }
    };

    void process_application2(size_t index_){
        auto &msg = receive_message[index_];
        Packet_num_len pkt_num = msg.get_packet_number();
        Offset_len pkt_offset = msg.get_packet_offset();
        Difference_len pkt_difference = msg.get_packet_difference();
        auto pkt_len = msg.get_packet_length();

        auto pkt_importance = msg.get_packet_importance();
        
        auto pkt_importance_blocks = msg.get_blocks(); /* Limit16_t: not complete statics, otherwise complete statics*/
        auto channel_status = msg.get_connection_status(); /* Unimportant channel complete status, 1 is complete, 0 is not. */
        
        if (pkt_num < current_loop_min){
            receive_slot[index_] = 0;
            return;
        }

        // if (pkt_len == 4) {
        //     std::cout<<"receive from:";
        //     ip_print(peeraddr);
        //     log_print(msg.iov[1].iov_base, 4);
        // }

        bool valid_pkt = pkt_difference >= receive_connection_difference;
        std::optional<size_t> expectedsize;
        if (valid_pkt){
            if (pkt_offset == 0){
                recvCQ.indexcheck(pkt_difference);
                if(!recvCQ.insertzero(pkt_difference, index_)){
                    receive_slot[index_] = 0;
                }else{
                    receive_slot[index_] = 1;
                    struct preamble {
                        size_t nbytes = 0;
                        size_t opcode = 0;
                        size_t slot = 0;
                        size_t offset = 0;
                        size_t length = 0;
                        size_t roffset = 0;
                    };
                    auto* preamble_header = reinterpret_cast<const preamble*>(msg.iov[1].iov_base);
                    if ((preamble_header->opcode & ~1) == 0){
                        expectedsize = sizeof(preamble) + preamble_header->length;
                    }else{
                        expectedsize = sizeof(preamble);
                    }
                }
            }else{
                receive_slot[index_] = 1;
            }
        }else{
            receive_slot[index_] = 0;
        }

        if (max_received == std::numeric_limits<size_t>::max() || pkt_num > max_received){
            max_received = pkt_num;
            send_num = pkt_num;
        }  
   
        size_t pos = pkt_num - current_loop_min;
  
        if(pos > 8000){
            pos = 0;
        }
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range. (byte_index:"<< byte_index <<", "<< receivevector.size() 
            <<", "<<max_received<<", "<< current_loop_min <<")" << std::endl;
            _Exit(0);
        }

        if (valid_pkt){
            receivevector[byte_index] |= (1 << bit_index);  
            bool exist = false;
            recvCQ.insert(pkt_difference, pkt_offset, pkt_len, index_, exist, pkt_importance, pkt_importance_blocks, channel_status);
            if (exist){
                receive_slot[index_] = 0;
            }

            if (expectedsize){
                recvCQ.setRead(pkt_difference, *expectedsize);
            }
        }
    }


    /*Max received index*/
    size_t boundary(){
        return receive_upper_limit;
    }

    /*Only send acknowledge packet*/
    size_t send_data2(){
        auto payload_ = 0;
        if (send_packet_type == Type::ACK){
            payload_ = send_acknowledge();
        }
        return payload_;
    }

    /*Only process handshake packet*/
    size_t recv_slice(uint8_t* src, size_t src_len, struct timespec ts = {0, 0}){
       
        auto pkt_ty = reinterpret_cast<Header *>(src)->ty;
        size_t read_ = 0;

        if (pkt_ty == Type::Handshake && is_server){
            if(handshake_completed){
                initial_rtt();
            }
            handshake_completed = true;
            initial = true;
        }

        //If receiver receives a Handshake packet, it will be prepared to send a Handshank.
        if (pkt_ty == Type::Handshake && !is_server){
            initial_rtt();
            handshake_confirmed = true;
        }

        return read_;
    };

    /*
    TODO（3/2）:
    1. skip pakcet older than this round minimum packet -> DONE
    2. add new flag to shrink receive message queue.
    */
    void process_application_packet(size_t index, bool isfirst){
        auto& msg = receive_message[index];
        Packet_num_len pkt_num = msg.get_packet_number();
        Offset_len pkt_offset = msg.get_packet_offset();
        Difference_len pkt_difference = msg.get_packet_difference();
        auto pkt_length = msg.get_packet_length();
        auto pkt_importance = msg.get_packet_importance();
        
        auto pkt_importance_blocks = msg.get_blocks(); /* Limit16_t: not complete statics, otherwise complete statics*/
        auto channel_status = msg.get_connection_status(); /* Unimportant channel complete status, 1 is complete, 0 is not. */

        if (pkt_num < current_loop_min){
            receive_slot[index] = 0;
            return;
        }
        // std::cout<<"Received:"<<pkt_difference<<", "<<pkt_num<<", "<<pkt_offset;
        std::optional<int> expectedsize;
        /*Mark packet as to be processed*/
        receive_slot[index] = 1;
        if (pkt_difference >= receive_connection_difference){
            if (pkt_offset == 0){
                if (recvCQ.differencecheck(pkt_difference)){
                    recvCQ.indexcheck(pkt_difference);
                    if(!recvCQ.insertzero(pkt_difference, index)){
                        receive_slot[index] = 0;
                    }else{
                        struct preamble {
                            size_t nbytes = 0;
                            size_t opcode = 0;
                            size_t slot = 0;
                            size_t offset = 0;
                            size_t length = 0;
                            size_t roffset = 0;
                        };
                        auto* preamble_header = reinterpret_cast<const preamble*>(msg.iov[1].iov_base);
                        if (preamble_header->opcode == 1 || preamble_header->opcode == 0){
                            expectedsize = sizeof(preamble) + preamble_header->length;
                        }else{
                            expectedsize = sizeof(preamble);
                        }
                    }
                }else{
                    receive_slot[index] = 0;
                }
            }
        }else{
            receive_slot[index] = 0;
        }

        if (max_received == std::numeric_limits<size_t>::max() || pkt_num > max_received){
            max_received = pkt_num;
            /* bit map substitude byte map*/
            send_num = pkt_num;
        }  

        
        size_t pos = pkt_num - current_loop_min;
  
        if(pos > 8000){
            std::memset(receivevector.data(), 0, receivevector.size());
            current_loop_min = pkt_num;
            pos = 0;
        }
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range. (byte_index:"<< byte_index <<", "<< receivevector.size() 
            <<", "<<max_received<<", "<< current_loop_min <<")" << std::endl;
            _Exit(0);
        }

        if (pkt_difference >= receive_connection_difference){
            receivevector[byte_index] |= (1 << bit_index);  
            // std::cout<<", "<<byte_index<<", "<<bit_index<<std::endl;
            bool exist = false;
            recvCQ.insert(pkt_difference, pkt_offset, pkt_length, index, exist, pkt_importance, pkt_importance_blocks, channel_status); 
            if (exist){
                receive_slot[index] = 0;
            }
            if (expectedsize){
                recvCQ.setRead(pkt_difference, *expectedsize);
            }
        }
    };

    bool received(size_t explen_){
        return recvCQ.isreceived(receive_connection_difference, explen_);
    }

    
    size_t send_acknowledge(){
        auto ty = Type::ACK;
 
        Header* hdr = reinterpret_cast<Header *>(acknowldge_header.data());
        hdr->ty = ty;
        hdr->pkt_num = send_num;
        hdr->offset = 0;

        auto i = 0;

        for ( ; i < recvCQ.size(); i++) 
        {
            if (recvCQ.at(i).get_difference() == (receive_connection_difference + i))
            {
                if (!recvCQ.at(i).is_complete2())
                {
                    break;
                } 
            }else
            {
                break;
            }
        }
        
        /*status lastest received difference*/
        hdr->difference = receive_connection_difference + i;
        // std::cout<<"send_acknowledge:"<<current_loop_min<<", "<<send_num<<", "<<hdr->difference<<std::endl;

        size_t info_len = (max_received - current_loop_min + 1 + 7) / 8;
        hdr->pkt_length = info_len + sizeof(Packet_num_len);

        acknowldge_iov[0].iov_base = acknowldge_header.data();
        acknowldge_iov[0].iov_len = sizeof(Header) - 5;

        ACKrange = current_loop_min;
        acknowldge_iov[1].iov_base = &ACKrange;
        acknowldge_iov[1].iov_len = sizeof(Packet_num_len);

        acknowldge_iov[2].iov_base = receivevector.data();
        acknowldge_iov[2].iov_len = info_len;

        acknowldge_msghdr.msg_iov = &acknowldge_iov[0];
        acknowldge_msghdr.msg_iovlen = 3;


        send_packet_type = ty;
        return sizeof(Header) - 5 + hdr->pkt_length;
    }
    

    bool check_status(){
        if (recovery.cwnd_enough() && sendbufferqueue.ready()) return true;
        return false;
    }

    /*Update received difference record*/
    void update_receive_parameter(){
        current_loop_min = current_loop_max + 1;
    }

     /*Check send or received data for function*/
    void log_print_fun(const char* func_name, void* src_, size_t len_) {
        if (!src_) {
            std::cerr <<"[" << func_name << "] Null pointer passed to log_print!" << std::endl;
            return;
        }
        
        auto* data = static_cast<uint8_t*>(src_);  
        std::cout << "[" << func_name << "]" << std::endl;
        for (size_t i = 0; i < len_; i++) {
            std::cout << static_cast<int>(data[i]) << " ";  
        }
        std::cout << std::endl;
    }

    void process_acknowledge(const size_t index_){
        auto& msg = receive_message[index_];
        auto pkt_num = msg.get_packet_number();
        auto pkt_len = msg.get_packet_length();
        auto pkt_difference = msg.get_packet_difference();
        receive_slot[index_] = 0;
      
        auto receivets = std::chrono::high_resolution_clock::now();
        auto first_pn = *reinterpret_cast<const uint64_t*>(msg.iov[1].iov_base);

        if (first_pn >= (max_acknowleged + 1)){
            auto ackts = tsInfo.removeBeforeValue(first_pn);
            if (ackts.has_value()){
                update_rtt(*ackts, receivets, receivets);
            }
        }
        sendbufferqueue.completecheck(pkt_difference);

        auto end_pn = pkt_num;
        bool loss = false;
        size_t total_send = end_pn - first_pn + 1;
        auto ack_src = reinterpret_cast<const uint8_t*>(msg.iov[1].iov_base) + sizeof(uint64_t);
        size_t byte_index = 0;
        size_t bit_index = 0;

        
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        auto pn = first_pn;

        // std::cout<<"process_acknowledge:"<<first_pn<<", "<<end_pn<<std::endl;
        // connection_map.forEachSlotAutoRangePartial(first_pn, (end_pn+1), [&](uint64_t pkt, const auto& slot){
        //     if (slot.difference < pkt_difference){
        //         if (++bit_index == 8) {
        //             bit_index = 0;
        //             ++byte_index;
        //         }
        //         /*Do nothing*/
        //         return;  
        //     }else{
        //         size_t value = (ack_src[byte_index] >> bit_index) & 1;
        //         // std::cout<<"ACK:"<<pkt_difference<<", "<<byte_index<<", "<<bit_index<<", ";
        //         sendbufferqueue.pkt2ack(slot.difference, slot.offset, pkt, (bool)value);    
        //         if (++bit_index == 8) {
        //             bit_index = 0;
        //             ++byte_index;
        //         }
        //     }
        // });

        connection_map.forEachSlotAutoRangePartial(first_pn, (end_pn+1), [&](uint64_t pkt, const auto& slot, const bool& delay){
            if (slot.difference < pkt_difference){
                if (++bit_index == 8) {
                    bit_index = 0;
                    ++byte_index;
                }
                /*Do nothing*/
                // return;  
            }else{
                size_t ack_value = (ack_src[byte_index] >> bit_index) & 1;
                if (delay){
                    if (ack_value) {
                        if (slot.channel == static_cast<uint8_t>(Channel::Unimportant)) {
                            if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_reliable)) {
                            /* Unimportant channel: unreliable */
                            sendbufferqueue.pkt2ack_unimportant(
                                slot.difference, 
                                slot.offset, 
                                pkt, 
                                (bool)ack_value, 
                                static_cast<PktStatus>(slot.pkt_status));
                            } else if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_unreliable)) {
                                /* Nothing to do */
                            } else {
                                /* Unimportant channel: Partial reliable */
                                if (static_cast<PktStatus>(slot.pkt_status) == PktStatus::Unimportant_partialreliable) {
                                    sendbufferqueue.pkt2ack_unimportant(slot.difference, slot.offset, pkt, (bool)ack_value, true)
                                } 
                                // else {
                                //     sendbufferqueue.pkt2ack_unimportant(slot.difference, slot.offset, pkt, (bool)ack_value, false)
                                // }
                            }
                        } else {
                            if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Important_reliable_special)) {
                                sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, (bool)ack_value, true); 
                            } else {
                                sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, (bool)ack_value, false); 
                            }  
                        }
                    }
                    if (++bit_index == 8) {
                        bit_index = 0;
                        ++byte_index;
                    }
                }else {
                    if (slot.channel == static_cast<uint8_t>(Channel::Unimportant)) {
                        if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_reliable)) {
                            /* Unimportant channel: unreliable */
                            sendbufferqueue.pkt2ack_unimportant(
                                slot.difference, 
                                slot.offset, 
                                pkt, 
                                (bool)ack_value, 
                                static_cast<PktStatus>(slot.pkt_status));
                        } else if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_unreliable)) {
                            /* Nothing to do */
                        } else {
                            /* Unimportant channel: Partial reliable */
                            sendbufferqueue.pkt2ack_unimportant(slot.difference, slot.offset, pkt, (bool)ack_value, static_cast<PktStatus>(slot.pkt_status))
                        }
                    } else {
                        /* Important channel: reliable */
                        if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Important_reliable_special)) {
                            sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, (bool)ack_value, true); 
                        } else {
                            sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, (bool)ack_value, false); 
                        } 
                        // sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, 
                        //     (bool)ack_value, slot.block_flag, slot.unimportant_flag);
                    }
                    if (++bit_index == 8) {
                        bit_index = 0;
                        ++byte_index;
                    }
                }
                
            }
        });

        if (max_acknowleged == LIMIT_UINT64_T){
            max_acknowleged = end_pn;
        }else{
            if (end_pn > max_acknowleged){
                max_acknowleged = end_pn;
            }
        }
        
        if (loss && !first_loss){
            recovery.check_point();
            recovery.congestion_event(receivets);
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
            first_loss = true;
        }else{
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
        }
    }

    /*Update receive difference to process next block data*/
    void update_receive_difference(){
        recvCQ.pop_front();
        receive_connection_difference++;
    }

    /*Check current receving data block status*/
    bool receive_complete(){
        if (receive_connection_difference == recvCQ.start() && !recvCQ.empty()){
            return recvCQ.iscomplete(receive_connection_difference);
        }
        return false;
    }

    void complete_check(){
        if (receive_connection_difference == recvCQ.start() && !recvCQ.empty()){
            recvCQ.startcomplete(receive_connection_difference);
            recvCQ.completecheck(receive_connection_difference);
        }
    }

    size_t recvCQcheck(){
        return recvCQ.get_status(receive_connection_difference);
    }


    void rx_len(size_t expected){
        recvCQ.rx_len(receive_connection_difference, expected);
    }

    void rx_set(size_t expected, uint8_t * target_){
        recvCQ.set_recv_pointer(receive_connection_difference, target_);
    }

    void set_send_time(){
        handshake = std::chrono::high_resolution_clock::now();
    }
    
    bool get_data(struct iovec* iovecs, int iovecs_len, int type_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
        bool completed = true;
	    dmludp_error_sent = 0;

        if (sendbufferqueue.full()){
            return false;
        }

        if (iovecs_len != 1) {
            if (iovecs[1].iov_len < 2 * 1024 * 1024){
                auto bitmaplen = iovecs[1].iov_len / MAX_SEND_UDP_PAYLOAD_SIZE + 1;

                std::span<uint64_t> bitmapview(&bitmap_vector[0], bitmaplen / 64 + 1);
                sendbufferqueue.push_back(iovecs, iovecs_len, type_, priotity_list, bitmapview, 0, bitmaplen);
            } else {
                sendbufferqueue.push_back(iovecs, iovecs_len, type_, priotity_list);
            }  
        } else {
            sendbufferqueue.push_back(iovecs, iovecs_len, type_, priotity_list);
        }

        
        
        set_handshake();
        return completed;
    }

    /*If timer triggered, process all unacknowledge packet as loss*/
    /*TODD: use previous record pair to minimize the iteration times*/
    void process_timeout(){
        auto pn = max_acknowleged + 1;
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        auto max_sent_pn = pkt_num_spaces.getpktnum();

        // connection_map.forEachSlotAutoRangePartial(pn, (max_sent_pn + 1), [&](uint64_t pkt, const auto& slot, const bool & delayed){
        //     if (slot.difference <= send_connection_difference){
        //         return;
        //     }else{
        //         // std::cout<<"Timout:";
        //         sendbufferqueue.pkt2ack(slot.difference, slot.offset, pkt, false, slot.block_flag, slot.unimportant_flag);
        //     }
        // });

         connection_map.forEachSlotAutoRangePartial(first_pn, (end_pn+1), [&](uint64_t pkt, const auto& slot, const bool& delay){
            if (slot.difference < pkt_difference){
                if (++bit_index == 8) {
                    bit_index = 0;
                    ++byte_index;
                }
                /*Do nothing*/
                // return;  
            }else{
                if (slot.channel == static_cast<uint8_t>(Channel::Unimportant)) {
                    if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_reliable)) {
                        /* Unimportant channel: unreliable */
                        sendbufferqueue.pkt2ack_unimportant(
                            slot.difference, 
                            slot.offset, 
                            pkt, 
                            false, 
                            false);
                    } else if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Unimportant_unreliable)) {
                        /* Nothing to do */
                    } else {
                        /* Unimportant channel: Partial reliable */
                        // sendbufferqueue.pkt2ack_unimportant(slot.difference, slot.offset, pkt, false, false)
                    }
                } else {
                    /* Important channel: reliable */
                    sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, false, false); 
                    // if (slot.pkt_status == static_cast<uint8_t>(PktStatus::Important_reliable_special)) {
                    //     sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, false, false); 
                    // } else {
                    //     sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, false, false); 
                    // } 
                    // sendbufferqueue.pkt2ack_important(slot.difference, slot.offset, pkt, 
                    //     (bool)ack_value, slot.block_flag, slot.unimportant_flag);
                }
                
            }
        });

        auto total_send = max_sent_pn - max_acknowleged;

        max_acknowleged = max_sent_pn;
        auto ackts = tsInfo.removeBeforeValue(max_acknowleged);

        auto receivets = std::chrono::high_resolution_clock::now();
        recovery.check_point();
        recovery.congestion_event(receivets);
        recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
    }

    // ssize_t prepareData() {
    //     Type ty = Type::Application;
    //     ssize_t out_len = 0; 
    //     Offset_len out_off = 0;
    //     size_t i = 0;
    //     ssize_t tramssioning_index = -1;

    //     size_t cwnd_limit = recovery.cwnd_available();
    //     if (cwnd_limit <= 0){
    //         return 0;
    //     }

    //     const size_t sent_limit = cwnd_limit;
    //     size_t sent = 0;      
    //     size_t sent_cwnd = 0; 
    //     auto sendbufferqueue_start_index = sendbufferqueue.start();
    //     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++) {
    //         i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //         int d_sent = 0;
    //         auto pkg_difference = sendbufferqueue.get_difference(i);
    //         while (true){
    //             size_t send_status = sendbufferqueue.get_status(i);
    //             if (i < 0 || i > sendbufferqueue.get_capacity() || sent > send_message.size()){
    //                 std::cout<<"i:"<<i<<", sent:"<<sent<<std::endl;
    //                 _Exit(0);
    //             }
    //             auto s_flag = sendbufferqueue.emit(i, send_message[sent].iov[1], out_len, out_off);
                
    //             if (out_len == -1) {
    //                 break;
    //             }
                
    //             auto pn = pkt_num_spaces.updatepktnum();
    //             // if (send_status == 1) {
    //             //     std::cout<<"sent:"<<pn<<", "<<pkg_difference<<", "<<out_off<<", "<<sendbufferqueue.at(idx).metabuf.lastpacketOffset<<std::endl;
    //             // }

       
    //             send_message[sent].setMessageHeader(pn, out_off, pkg_difference, (Packet_num_len)out_len);
    //             recovery.on_packet_sent(out_len);

    //             connection_map.push(out_off, pkg_difference);
    //             // connection_map.push(out_off, pkg_difference, sendRound);

    //             sent++;
    //             sent_cwnd += out_len;
    //             d_sent++;
    //             if (sent_cwnd >= sent_limit || sent >= send_message.size()){
    //                 break;
    //             }           
    //         }
    //         if (sent_cwnd >= sent_limit || sent >= send_message.size()){
    //             break;
    //         }
    //     }

    //     return sent;
    // }

    /*
    1. first transmission: important + unimportant received, no action
    2. Only important retransmission, status mark as 2 and header add important blocks
    3. Only unimportant retransmssion, status mark as 1 and header add important block; 
        After retransmissions, just send elicit packet with status 2 and important block to let receiver known.
    */
    ssize_t prepareData() {
        Type ty = Type::Application;
        ssize_t out_len = 0; 
        Offset_len out_off = 0;

        size_t i = 0;
        ssize_t tramssioning_index = -1;

        size_t cwnd_limit_important = recovery.cwnd_available();
        size_t cwnd_limit_unimportant = low_recovery.cwnd_available();
        if (cwnd_limit_important <= 0 && cwnd_limit_unimportant <= 0){
            return 0;
        }

        const size_t sent_limit_important = cwnd_limit_important;
        const size_t sent_limit_unimportant = cwnd_limit_unimportant;

        /* send_message index */
        size_t sent = 0;

        size_t sent_important = 0;      
        size_t sent_cwnd_important = 0; 

        size_t sent_unimportant = 0;      
        size_t sent_cwnd_unimportant = 0; 

        auto sendbufferqueue_start_index = sendbufferqueue.start();
        for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++) {
            i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
            auto pkg_difference = sendbufferqueue.get_difference(i);

            /* Important */
            if (sent_cwnd_important < sent_limit_important){
                Block_len out_blocks = std::numeric_limits<Block_len>::max();
                Status_len out_status = 0;
                while (true){
                    size_t send_status = sendbufferqueue.get_status(i);
                    bool isElicit = false;
                    if (i < 0 || i > sendbufferqueue.get_capacity() || sent > send_message.size()){
                        std::cout<<"i:"<<i<<", sent:"<<sent<<std::endl;
                        _Exit(0);
                    }
                    auto s_flag = sendbufferqueue.emit_important(i, send_message[sent].iov[1], out_len, out_off, out_blocks, out_status);
                    
                    if (out_len == -1) {
                        break;
                    }
                    
                    auto pn = pkt_num_spaces.updatepktnum();
                    isElicit = out_off == ELICIT_OFFSET;
                    /* Elicit or not*/
                    if (isElicit) {
                        send_message[sent].setMessageHeader(pn, ELICIT_OFFSET, pkg_difference, (Packet_num_len)out_len, Channel::Important, out_blocks, out_status, Type::ElicitAck);
                    } else {
                        send_message[sent].setMessageHeader(pn, out_off, pkg_difference, (Packet_num_len)out_len, Channel::Important, out_blocks, out_status);
                    }
        
                    recovery.on_packet_sent(out_len);

                    if (isElicit) {
                        // connection_map.push(0, pkg_difference, 1, Type::ElicitAck);
                        connection_map.push(ELICIT_OFFSET, pkg_difference, out_blocks, static_cast<uint8_t>(PktStatus::Important_reliable), static_cast<uint8_t>(ty));
                    } else {
                        // connection_map.push(out_off, pkg_difference, 1);
                        if (out_status){
                            connection_map.push(out_off, pkg_difference, out_blocks, static_cast<uint8_t>(PktStatus::Important_reliable_special), static_cast<uint8_t>(ty));
                        } else {
                            connection_map.push(out_off, pkg_difference, out_blocks, static_cast<uint8_t>(PktStatus::Important_reliable), static_cast<uint8_t>(ty));

                        }
                    }

                    sent++;
                    sent_cwnd_important += out_len;
                    if (sent_cwnd_important >= sent_limit_important || sent >= send_message.size()){
                        break;
                    }           
                }
            }
            

            /* Unimportant */
            if (sent_cwnd_unimportant < sent_limit_unimportant) {
                bool pkt_elicit = false;
                Block_len out_blocks = std::numeric_limits<Block_len>::max();
                PktStatus pkt_status;
                while (true){
                    size_t send_status = sendbufferqueue.get_status(i);
                    if (i < 0 || i > sendbufferqueue.get_capacity() || sent > send_message.size()){
                        std::cout<<"i:"<<i<<", sent:"<<sent<<std::endl;
                        _Exit(0);
                    }

                    auto s_flag = sendbufferqueue.emit_unimportant(i, send_message[sent].iov[1], out_len, out_off, out_blocks, pkt_status);
                    
                    if (out_len == -1) {
                        break;
                    }
                    
                    auto pn = pkt_num_spaces.updatepktnum();

                    if (out_off == ELICIT_OFFSET) {
                        /* Single Elicit packet */
                        send_message[sent].setMessageHeader(pn, out_off, pkg_difference, (Packet_num_len)out_len, Channel::Important, out_blocks, pkt_status, static_cast<uint8_t>(Tpye::Elicit));
                        recovery.on_packet_sent(out_len);

                        connection_map.push(out_off, pkg_difference, out_blocks, static_cast<uint8_t>(PktStatus::Important_reliable), static_cast<uint8_t>(Tpye::Elicit));
                    } else {
                        bool complete_flag = pkt_status == PktStatus::Unimportant_partialreliable;
                        send_message[sent].setMessageHeader(pn, out_off, pkg_difference, (Packet_num_len)out_len, Channel::Unimportant, out_blocks, (uint8_t)complete_flag);
                        recovery.on_packet_sent(out_len);

                        connection_map.push(out_off, pkg_difference, out_blocks,static_cast<uint8_t>(pkt_status), ty);
                    }
                    

                    sent++;
                    sent_cwnd_unimportant += out_len;
                    if (sent_cwnd_unimportant >= sent_limit_unimportant || sent >= send_message.size()){
                        break;
                    }           
                }
            }
            
            if ((sent_cwnd_important >= sent_limit_important && sent_cwnd_unimportant >= sent_limit_unimportant) || sent >= send_message.size()){
                break;
            }
        }

        return sent;
    }

    /*Prepare send packets*/
    std::pair<ssize_t, ssize_t> send_packet(){
        if (get_dmludp_error()){
            return std::make_pair(start_index, end_index);
        }

        if (end_index == -1){
            end_index = prepareData() - 1;
            if(end_index != -1){
                start_index = 0;
            }
            send_packet_type = Type::Application;
        }
        return std::make_pair(start_index, end_index);
    }

    /*Use to clear send parameter*/
    void send_packet_complete(size_t err_ = 0, size_t sent = 0, const std::chrono::high_resolution_clock::time_point& start_ts = std::chrono::high_resolution_clock::time_point{}){
        if(send_packet_type == 0){
            return;
        }
        set_error2(err_);
        if (err_ != 0){
            if (send_packet_type == Type::Application){
                end_ts = std::chrono::high_resolution_clock::now();
                if (start_index < 0){
                    std::cout<<"send_packet_complete start_index < 0" <<std::endl;
                    _Exit(0);
                }
                tsInfo.updateQueue(send_message[start_index].get_packet_number(), start_ts, pkt_num_spaces.getpktnum(), end_ts);
                start_index = start_index + sent;
            }
            return;
        }
        
        if(send_packet_type == Type::ACK){
            process_application_copy();
        }else if(send_packet_type == Type::Application){
            end_index = -1;
            set_handshake();
            tsInfo.updateQueue(send_message[start_index].get_packet_number(), start_ts, pkt_num_spaces.getpktnum(), end_ts);
        }else if(send_packet_type == Type::ElicitAck){

        }else if(send_packet_type == Type::Stop){

        }else if(send_packet_type == Type::Fin){

        }

        send_packet_type = 0;
    }

    void update_boundary(){
        // memset(receivevector.data(), 0, receivevector.size());
        for (auto& v : receivevector) v = 0;
        current_loop_min = max_received + 1;
    }

    size_t get_slot(){
        return slot_index;
    }

    void update_slot(){
        slot_index = (slot_index + 1) % RX_CONST;
        while (receive_slot[slot_index] != 0)
        {
           slot_index = (slot_index + 1) % RX_CONST;
        }
    }

    size_t get_start(){
        return next_available(std::numeric_limits<size_t>::max());
    }

    size_t get_end(){
        return receive_message.size();
    }


    size_t next_available(size_t index){
        if (index == std::numeric_limits<size_t>::max()){
            auto idx = 0;
            while(true){
                if (receive_slot[idx] == 0){
                    break;
                }
                idx++;
                if (idx == receive_slot.size()){
                    return std::numeric_limits<size_t>::max();
                }
            }
            return idx;
        }
        auto idx = index + 1;
        while(true){
            if (receive_slot[idx] == 0){
                break;
            }
            idx++;
            if (idx == receive_slot.size()){
                return std::numeric_limits<size_t>::max();
            }
        }
        return idx;
    }


    bool registration_check(){
        return receive_connection_difference == receive_connection_difference_registration;
    }

    void process_application_copy(){
        Offset_len pkt_offset;
        Packet_len pkt_len;
        Difference_len pkt_difference;

        int copycount = 0;
        if (!recvCQ.empty()){
            if (receive_connection_difference == recvCQ.start() && (recvCQ.get_status(receive_connection_difference) == 2) && recvCQ.srcsetcheck(receive_connection_difference)){
                auto index = recvCQ.startpos();
                auto& msg = receive_message[index]; 
                pkt_offset = msg.get_packet_offset();
                pkt_difference = msg.get_packet_difference();
                recvCQ.copy(pkt_difference, pkt_offset, msg.iov[1].iov_base, 48);
                copycount += 48;
                receive_slot[index] = 0;
                receive_record.reset();
                receive_connection_difference_registration = receive_connection_difference;
                return;
            }
        }

        /*TODO: used count to reduce iteration times*/
        for (auto index = 0; index < receive_slot.size(); index++){
            auto& msg = receive_message[index];
            pkt_offset = msg.get_packet_offset();
            pkt_len = msg.get_packet_length();
            pkt_difference = msg.get_packet_difference();

            if (receive_slot[index] == 0){
                continue;
            }

            /*No longer process old data block*/
            if (pkt_difference < receive_connection_difference){
                receive_slot[index] = 0;
                continue;
            }

            if (receive_connection_difference == pkt_difference && recvCQ.targetCheck(pkt_difference)){
                
                receive_slot[index] = 0;
                if (!recvCQ.copyed_check(pkt_difference, pkt_offset)){
                    if (pkt_offset >= 48){
                        // std::cout<<"2 copy:"<<pkt_offset<<std::endl;
                        recvCQ.copy(receive_connection_difference, (pkt_offset - 48), msg.iov[1].iov_base, pkt_len);
                        copycount += pkt_len;
                    }else{
                        recvCQ.copy(receive_connection_difference, (pkt_offset), msg.iov[1].iov_base, pkt_len);
                        copycount += pkt_len;
                    }

                    if(recvCQ.processComplete(pkt_difference)){
                        return;
                    }

                    continue;
                }
            }
        }
        
        // std::cout<<"copycount:"<<copycount;
        recvCQ.processCheck(receive_connection_difference);       
    }

    size_t get_error_sent(){
        return dmludp_error_sent;
    }

    size_t get_dmludp_error(){
        return dmludp_error;
    }

    void set_error2(size_t err){
        dmludp_error = err;
    }

    void set_error(size_t err, size_t application_sent){
        dmludp_error = err;
	    if (application_sent != 0){
            dmludp_error_sent += application_sent;
        }else{
            dmludp_error_sent = 0;
        }
    }

    bool transmission_complete(){
        if (sendbufferqueue.iscomplete(sendbufferqueue.start())){
            sendbufferqueue.pop_front();
            return true;
        }
        return false;
    }

    bool transmission_complete_check(){
        bool transmission_complte = false;
        while(transmission_complete()){
            transmission_complte = true;
        }
        return transmission_complte;
    }

    //Send single packet
    size_t send_data(uint8_t* out){
        size_t total_len = HEADER_LENGTH;

        uint64_t psize = 0;

        auto ty = write_pkt_type(); 

        if (ty == Type::Handshake && server){
            memcpy(out, handshake_header, HEADER_LENGTH);
            set_handshake();
        }

        if (ty == Type::Handshake && !server){
            memcpy(out, handshake_header, HEADER_LENGTH);
            initial = true;
        }

        if (ty == Type::ACK){
            
        }      

        total_len += (size_t)psize;

        return total_len;
    };


    size_t send_data_stop(uint8_t* out){ 
        size_t total_len = HEADER_LENGTH;

        auto pn = pkt_num_spaces.updatepktnum();

        uint64_t offset = 0;
        uint8_t priority = 0;
        uint64_t psize = 0;

        auto ty = Type::Stop; 

        Header* hdr = new Header(ty, pn, offset, send_connection_difference, psize);

        memcpy(out, hdr, HEADER_LENGTH);
        delete hdr; 
        hdr = nullptr; 

        return total_len;
    };

    size_t send_data_handshake(uint8_t* out){     
        size_t total_len = HEADER_LENGTH;
        memcpy(out, handshake_header, HEADER_LENGTH);
        set_handshake();
        return total_len;
    };

    bool is_stopped(){
        return stop_flag && stop_ack && initial;
    };

    void set_handshake(){
        handshake = std::chrono::high_resolution_clock::now();
        end_ts = handshake;
    };

    double get_rtt() {
        return rto.count();
    };   

    /// Returns true if the connection handshake is complete.
    bool is_established(){
        return handshake_completed;
    };

    /// Returns true if the connection is closed.
    /// If this returns true, the connection object can be dropped.
    bool is_closed() {
        return closed;
    };
    
    Type write_pkt_type(){
        if (rto.count() == 0 && is_server == true){
            handshake_completed = true;
            return Type::Handshake;
        }

        if (handshake_confirmed == false && is_server == false){
            handshake_confirmed = true;
            return Type::Handshake;
        }

        if (rtt.count() != 0){
            return Type::Application;
        }

        return Type::Unknown;
    };
    
};

}