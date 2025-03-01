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
#include "send_buf.h"
#include <cmath>
#include <unordered_map>

namespace dmludp {

const size_t HEADER_LENGTH = sizeof(Header);

// The default max_datagram_size used in congestion control.
const size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1440;

const size_t RX_CONST = 6000;

const size_t ONCE_LIMIT = 1300;

const size_t ONCE_SEND_LIMIT = ONCE_LIMIT;

const size_t ONCE_RECEIVE_LIMINT = ONCE_LIMIT;

const double alpha = 0.875;

const double beta = 0.25;

using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Priority_len = uint8_t;

using Offset_len = uint32_t;

using Acknowledge_sequence_len = uint64_t;

using Difference_len = uint8_t;

using Acknowledge_time_len = uint8_t;

using Packet_len = uint16_t;

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

// class RecordInfo{
//     private:
//         ssize_t record_offset = 0;

//         size_t record_len = 0;

//         ssize_t record_difference = -1;

//     public:
//     RecordInfo(){};

//     ~RecordInfo(){};

//     void updateInfo(ssize_t offset, size_t len, ssize_t diff){
//         record_offset = offset;
//         record_len = len;
//         record_difference = diff;
//     }

//     void clear(){
//         record_offset = 0;
//         record_len = 0;
//         record_difference = -1;
//     }
// };


/*
Redesign Message with GSO(40), RCMessage for GRO(45))
*/
class Message{
    public:
        struct msghdr message_body;

        iovec iov[2];

        Header message_header;

        // char control[CMSG_SPACE(sizeof(struct timespec))]; 

        Message(){
            iov[0].iov_base = static_cast<void*>(&message_header);
            iov[0].iov_len = sizeof(Header);

            iov[1] = {nullptr, 0};

            memset(&message_body, 0, sizeof(msghdr));
            message_body.msg_iov = iov;
            message_body.msg_iovlen = 2; // Fixed to 2 iovecs
            // message_body.msg_control = control;     
            // message_body.msg_controllen = sizeof(control);
        }

        ~Message(){};

        void setMessageBody(void* buffer, size_t length) {
            iov[1].iov_base = buffer;
            iov[1].iov_len = length;
        }

        void setMessageHeader(uint64_t pn, uint32_t offset, uint8_t difference, uint16_t length) {
            message_header.pkt_num = pn;
            message_header.offset = offset;
            message_header.difference = difference;
            message_header.pkt_length = (Packet_num_len)length;
        }

        uint64_t get_packet_number(){
            return message_header.get_pkt_num();
        }

        uint8_t get_packet_type(){
            return message_header.get_ty();
        }

        uint32_t get_packet_offset(){
            return message_header.get_offset();
        }

        uint8_t get_packet_difference(){
            return message_header.get_difference();
        }

        uint16_t get_packet_length(){
            return message_header.get_pkt_length();
        }


        msghdr* getMessageHeader() {
            return &message_body;
        }
};

class RCMessage : public Message {
    public:
        RCMessage(){}
            
        void set_receive_message(void *ptr, size_t ptr_len){
            iov[1].iov_base = ptr;
            iov[1].iov_len = ptr_len;
        }

        ~RCMessage(){};
};

class TSCircularQueue{
private:
    using DataType = std::pair<std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>,
                               std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>>;
    using TimeStamp = std::chrono::high_resolution_clock::time_point;
    std::vector<DataType> buffer; 
    size_t head;                  
    size_t tail;                 
    size_t capacity;              
    size_t count;                 

public:
    explicit TSCircularQueue(size_t capacity = 25)
        : buffer(capacity), head(0), tail(0), capacity(capacity), count(0) {}

    ~TSCircularQueue(){}

    void enqueue(const DataType& value) {
        if (isFull()) {
            throw std::overflow_error("TSCircularQueue is full(enqueue)");
        }
        buffer[tail] = value;
        tail = (tail + 1) % capacity;
        ++count;
    }

    DataType dequeue() {
        if (isEmpty()) {
            throw std::underflow_error("TSCircularQueue is empty(deque)");
        }
        DataType value = buffer[head];
        head = (head + 1) % capacity;
        --count;
        return value;
    }

    DataType front() const {
        if (isEmpty()) {
            throw std::underflow_error("TSCircularQueue is empty(front)");
        }
        return buffer[head];
    }

    bool isEmpty() const {
        return count == 0;
    }

    bool isFull() const {
        return count == capacity;
    }

    size_t size() const {
        return count;
    }

    void clear() {
        head = 0;
        tail = 0;
        count = 0;
    }

    TimeStamp removeBeforeValue(uint64_t value) {
        if (isEmpty()) {
            throw std::underflow_error("TSCircularQueue is empty(remove)");
        }

        TimeStamp result;

        size_t indexToDeleteUpTo = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < count; ++i) {
            size_t actualIndex = (head + i) % capacity;
            const auto& item = buffer[actualIndex];
            if (item.first.first <= value || item.second.first >= value) {
                if (item.first.first == item.second.first){
                    result = item.first.second;
                }else{
                    result = item.first.second + (item.second.second - item.first.second) * (value - item.first.first) / (item.second.first - item.first.first) ; 
                }
                indexToDeleteUpTo = i; 
                break;
            }
        }

        if (indexToDeleteUpTo == std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("Value not found in the queue");
        }

        head = (head + indexToDeleteUpTo) % capacity;
        count -= indexToDeleteUpTo;
        return result;
    }

    void updateQueue(uint64_t key1, TimeStamp ts1, uint64_t key2, TimeStamp ts2) {
        enqueue({{key1, ts1}, {key2, ts2}});
    }

    void printQueue() const {
        std::cout << "Queue contents: " << std::endl;
        for (size_t i = 0; i < count; ++i) {
            size_t actualIndex = (head + i) % capacity;
            const auto& item = buffer[actualIndex];
            std::cout << "[" << item.first.first << ", " << item.second.first << "]" << std::endl;
        }
    }
};

/*Check usage*/
class RCset{
    public:
        boost::dynamic_bitset<> RCset_body;

        size_t payload_len;

        std::vector<size_t> dataload_len;

        size_t dataload_index = 0;

        RCset(size_t pkt_info = MAX_SEND_UDP_PAYLOAD_SIZE, size_t capacity_ = 80000): 
        RCset_body(capacity_),
        payload_len(pkt_info){
            dataload_len.reserve(10);
        }

        bool find(uint64_t offset_){
            auto index = get_index(offset_);
            return RCset_body[index] == 1;
        }

        void insert(uint64_t offset_){
            auto index = get_index(offset_);
            RCset_body[index] = 1;
        }

        void add_rule(size_t load_len){
            dataload_len.push_back(load_len);
        }

        // size_t get_index(size_t offset_){
        //     ssize_t index = -1;
        //     if (dataload_len.size() == 1){
        //         index = offset_ / payload_len;
        //     }else{
        //         if(offset_ <= dataload_len[0]){
        //             index = offset_ / payload_len;
        //         }else{
        //             index = round_up(dataload_len[0] , payload_len) - 1 + round_up((offset_ - dataload_len[0]), payload_len);
        //         }
        //     }
        //     return index;
        // }

        size_t get_index(size_t offset_){
            ssize_t index = -1;
            if (offset_ < 48){
                index = 0;
            }else{
                index = round_up((offset_ - 48), payload_len) + 1;
            }
            return index;
        }

        size_t round_up(size_t a, size_t b){
            if (b == 0) {
                throw std::invalid_argument("Division by zero is not allowed");
            }

            return (a + b - 1) / b;
        }

        void clear(){
            RCset_body.reset();
            dataload_len.clear();
            dataload_index = 0;
        }

        ~RCset(){}
};

class ReTransmissionMap{
    private:
        ssize_t start_packet = -1;

        ssize_t end_packet = -1;

        std::vector<uint32_t> offsets;

    public:
        ReTransmissionMap():offsets(100){};

        ~ReTransmissionMap(){};

        void clear(){
            start_packet = end_packet = -1;
        }

        uint32_t get_offset(uint64_t packetnum){
            return offsets[packetnum - start_packet];
        }

        void add(uint64_t packetnum, uint32_t packetoffset){
            if(start_packet == -1){
                start_packet = packetnum;
            }
            if (start_packet == -1){
                throw std::underflow_error("Error: start_packet is -1");
            }
            end_packet = packetnum;
            offsets[end_packet - start_packet] = packetoffset;
        }

        bool empty(){
            if (start_packet == -1 && end_packet != -1) {
                throw std::underflow_error("Error: start == -1, end != -1");
            }
            return ((start_packet == end_packet) && (start_packet == -1));
        }

        bool inrange(size_t PacketNum){
            return (PacketNum >= start_packet && PacketNum <= end_packet);
        }
};

class TransmissionMap{
    private:
        std::pair<ssize_t, uint32_t> startmap;

        std::pair<ssize_t, uint32_t> endmap;
    public:
        TransmissionMap() : startmap(-1, 0), endmap(-1, 0){};

        ~TransmissionMap(){};

        void add(uint64_t packetnum, uint32_t packetoffset){
            if(startmap.first == -1){
                startmap = std::make_pair(packetnum, packetoffset);
            }
            if(startmap.first == -1){
                throw std::underflow_error("Error: startmap.first is -1");
            }
            endmap = std::make_pair(packetnum, packetoffset);
        }

        bool empty() {
            if (startmap.first == -1 && endmap.first != -1) {
                throw std::underflow_error("Error: start == -1, end != -1");
            }
            return ((startmap.first == endmap.first) && (endmap.first == -1));
        }  

        void clear(){
            startmap = endmap = std::make_pair(-1, 0);
        }

        std::pair<uint64_t, uint64_t> get_range(){
            return std::make_pair(startmap.first, endmap.first);
        }

        uint32_t get_offset(uint64_t packetnum_){
            uint32_t offset_ = 0;
            if (startmap.second == 0){
                if (packetnum_ == startmap.first){
                    offset_ = startmap.second;
                }else{
                    offset_ = 48 + (packetnum_ - startmap.first - 1) * MAX_SEND_UDP_PAYLOAD_SIZE;
                }
            }else{
                offset_ = (packetnum_ - startmap.first) * MAX_SEND_UDP_PAYLOAD_SIZE + startmap.first;
            }
            return offset_;
        }

        bool inrange(size_t PacketNum){
            return (PacketNum >= startmap.first && PacketNum <= endmap.first);
        }
};


class MetaInfo{
    public:
        SendBuf metabuf;

        const uint8_t MetaDifference;

        ReTransmissionMap retransmission_map;

        std::vector<uint32_t> priority_offset;

        TransmissionMap transmission_map;

        size_t difference_flag;

        size_t range_len;

        std::pair<ssize_t, ssize_t> packet_range{-1, -1};

        MetaInfo(size_t difference_flag_ = 0): 
        difference_flag(difference_flag_), 
        MetaDifference(difference_flag_),
        metabuf(MAX_SEND_UDP_PAYLOAD_SIZE){};

        ~MetaInfo(){};

        void add_transmission(uint64_t packetnum_, uint32_t packetoffset_){
            retransmission_map.add(packetnum_, packetoffset_);
            if (packet_range.first == -1){
                packet_range = std::make_pair(packetnum_, packetnum_);
            }else{
                if (packet_range.second < packetnum_){
                    packet_range.second = packetnum_;
                }
            }
        }

        void add_retransmission(uint64_t packetnum_, uint32_t packetoffset_){
            transmission_map.add(packetnum_, packetoffset_);
            if (packet_range.first == -1){
                packet_range = std::make_pair(packetnum_, packetnum_);
            }else{
                if (packet_range.second < packetnum_){
                    packet_range.second = packetnum_;
                }
            }
        }

        void reset_packet_range(){
            packet_range = std::make_pair(-1, -1);
        }

        std::pair<uint64_t, uint64_t> get_packet_range(){
            return packet_range;
        }

        bool intransmissionmap(uint64_t PacketNum){
            return transmission_map.inrange(PacketNum);
            // if (PacketNum >= transmission_map.startmap.first && PacketNum <= PacketNum.endmap.first){
            //     return true;
            // }
            // return false;
        }

        bool inretransmissionmap(uint64_t PacketNum){
            return retransmission_map.inrange(PacketNum);
            // if (PacketNum >= retransmission_map.start_packet && PacketNum <= retransmission_map.end_packet){
            //     return true;
            // }
            // return false;
        }

        ssize_t offset_calculate(uint64_t PacketNum) {
            if (intransmissionmap(PacketNum)){
                return transmission_map.get_offset(PacketNum);
            }
            if (inretransmissionmap(PacketNum)){
                return retransmission_map.get_offset(PacketNum);
            }
            throw std::underflow_error("Error: packet doesn't belong to this block.");
            return -1;
        }

        void ack4offset(uint64_t PacketNum, bool isReceived, uint32_t offset_ = 0){
            auto packet_offset_ = offset_calculate(PacketNum);
            if (isReceived){
                metabuf.acknowledege_and_drop(packet_offset_, true);
            }else{
                metabuf.acknowledege_and_drop(packet_offset_, false);
            }
        }

        uint8_t get_difference(){
            return MetaDifference;
        }

        void set_buffer(struct iovec* iovecs, int iovecs_len, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
            metabuf.add_Meta(iovecs, iovecs_len);
            range_len = 0;
            for (auto i = 0; i < iovecs_len; i++){
                range_len += (iovecs[i].iov_len + MAX_SEND_UDP_PAYLOAD_SIZE - 1) / MAX_SEND_UDP_PAYLOAD_SIZE;
            }
        }

        bool iscomplete(){
            return metabuf.written_complete();
        }

        size_t sent(){
            return metabuf.sentComplete();
        }

        void clear(){

        }

};


class SCircularQueue {
    public:
        std::vector<MetaInfo> data_;
        size_t head_;
        size_t tail_;
        size_t capacity_;
        size_t count_ = 0;

        SCircularQueue(size_t capacity = 256) 
            : head_(0), tail_(0), capacity_(capacity)
        {
            data_.reserve(capacity);
            for (auto i = 0; i < capacity ; i++){
                data_.emplace_back(i);
            }
        }

        void push_back(struct iovec* iovecs, int iovecs_len, const std::vector<std::vector<uint8_t>> &priotity_list = {}) {
            data_[tail_].set_buffer(iovecs, iovecs_len, priotity_list);
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
            return head_ == tail_;
        }

        bool ready() {
            if(empty()){
                return false;
            }
            bool isReady = false;
            for(auto i = start(); i < end() ; i = (i + 1) % capacity_){
                isReady |= !data_[i].metabuf.is_empty();
            }
            return isReady;
        }

        bool full() const {
            return count_ == capacity_;
            return ((tail_ + 1) % capacity_) == head_;
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

        // ssize_t retransmision_available() {
        //     ssize_t index_ = -1;
        //     for (auto i = start() ; i < end(); i++){
        //         if (data_[i].transmission_status == 4){
        //             index_ = i;
        //             break;
        //         }
        //     }
        //     return index_;
        // }

        // size_t partial_check() {
        //     ssize_t index_ = -1;
        //     for (auto i = start() ; i < end(); i++){
        //         if (data_[i].status == 2){
        //             index_ = i;
        //             break;
        //         }
        //     }
        //     return index_;
        // }

        // ssize_t first_transmit_check() {
        //     ssize_t index_ = -1;
        //     for (auto i = start() ; i < end(); i++){
        //         if (data_[i].status == 1){
        //             index_ = i;
        //             break;
        //         }
        //     }
        //     return index_;
        // }

        // void push_back(struct iovec* iovecs, int iovecs_len, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
        //     data_[tail_].set_buffer(iovecs, iovecs_len, priotity_list);
        //     tail_ = (tail_ + 1) % capacity_;
        // }

        ~SCircularQueue() = default; 
};

class RecordInfo
{
private:
    /* data */
    uint8_t record_diffference = 0;

    uint32_t record_acumulate = 0;

    /*Continuous end index*/
    ssize_t end_index = -1;

    /*Continuous start index*/
    ssize_t start_index = -1;

    uint32_t record_offset = 0;
public:
    RecordInfo(/* args */);
    ~RecordInfo();
    void set(uint16_t index_, uint32_t len_, uint32_t offset_){
        start_index = end_index = index_;
        record_acumulate = len_;
        record_offset = offset_;
    } 

    void reset(){
        start_index = end_index = -1;
        record_acumulate = 0;
    }

    bool empty(){
        return (end_index == -1) && (start_index == -1);
    }

    void update(uint32_t index_, size_t offset_, size_t len_){
        if (get_start_index() == -1){
            set(index_, len_, index_);
        }else{
            end_index = index_;
            record_acumulate += len_;
            /*Add check for record len_*/
        }
    }

    size_t get_target_offset(){
        return (record_offset + record_acumulate);
    }

    size_t get_acumulation(){
        return record_acumulate;
    }

    size_t get_start_index(){
        return start_index;
    }

    size_t get_end_index(){
        return end_index;
    }
};

class metarecebuf{
    public:
        RecvBuf metabuf;

        RCset receive_offset;

        std::vector<uint16_t> source_len;

        bool complete_flag = false;

        uint8_t rdifference;

        size_t received = 0;

        bool has_zero = false;

        size_t srcset = 0;

        metarecebuf(uint8_t difference_): rdifference(difference_){
            for(auto i = 0; i < 2 ; i++){
                source_len.push_back(0);
            }
        }

        ~metarecebuf(){}

        void clear(){
            receive_offset.clear();
            metabuf.reset();
            metabuf.src = nullptr;
            complete_flag = false;
            received = 0;
            has_zero = false;
            srcset = 0;
            for (auto &e:source_len){
                e = 0;
            }
        }

        void addexplen(size_t exp_){
            for(auto &e: source_len){
                if (e == 0){
                    e = exp_;
                    break;
                }
            }
        }

        void set_complete(){
            complete_flag = true;
        }

        bool find(uint64_t pkt_offset, uint32_t pkt_length){
            auto exist = receive_offset.find(pkt_offset);
            if (!exist){
                if (pkt_offset == 0){
                    has_zero = true;
                }
                receive_offset.insert(pkt_offset);
                metabuf.reg(pkt_length);
                received += pkt_length;
            }
            return exist;
        }

        bool is_complete() const{
            size_t total = 0;
            for(auto e:source_len){
                total += e;
            }
            if (received == total || complete_flag){
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
};  

class RCircularQueue {
public:
    std::vector<metarecebuf> data_; // 存储所有 metarecebuf 对象
    size_t head_;      // 指向队头元素（最旧的数据）
    size_t tail_;      // 指向下一个写入位置
    size_t capacity_;  // 队列总容量
    size_t count_;     // 当前有效元素个数

    // 构造函数，默认容量为256
    RCircularQueue(size_t capacity = 256)
        : head_(0), tail_(0), capacity_(capacity), count_(0)
    {
        data_.reserve(capacity_);
        // 初始化每个元素（例如：根据下标初始化）
        for (size_t i = 0; i < capacity_; i++) {
            data_[i] = metarecebuf(static_cast<int>(i));
        }
    }

    // push_back 操作：复位 tail_ 指向的对象，并推进 tail_ 指针。
    // 如果队列已满，则覆盖最旧数据，同时 head_ 前进。
    bool push_back() {
        if (count_ == capacity_){
            return false;
        }
        // 在写入前先复位目标对象
        data_[tail_].clear();

        // 写入后推进 tail_
        tail_ = (tail_ + 1) % capacity_;

        // 如果队列已满，则覆盖旧数据：head_ 同步推进
        // if (count_ == capacity_) {
        //     head_ = tail_;
        // } else {
        //     ++count_;
        // }
        ++count_;

        return true;
    }

    // pop_front 操作：移除队头数据（复位对象并推进 head_ 指针）
    void pop_front() {
        if (empty()) {
            throw std::runtime_error("Queue is empty, cannot remove element.");
        }
        data_[head_].clear();
        head_ = (head_ + 1) % capacity_;
        --count_;
    }

    // 返回当前队列中的元素个数
    size_t size() const {
        return count_;
    }

    // 队列为空则返回 true
    bool empty() const {
        return count_ == 0;
    }

    // 队列满则返回 true
    bool full() const {
        return count_ == capacity_;
    }

    // 清空队列，复位 head_、tail_、count_，并复位所有数据
    void clear() {
        for (size_t i = 0; i < capacity_; i++) {
            data_[i].clear();
        }
        head_ = tail_ = count_ = 0;
    }

    // 返回当前 tail_（下一个写入位置）的下标
    size_t end() const {
        return tail_;
    }

    // 返回当前 head_（最旧元素）的下标
    size_t start() const {
        return head_;
    }

    // 基于下标对指定 metarecebuf 调用 find 操作
    // difference 作为下标参数，要求在 [0, capacity_) 范围内
    void insert(uint8_t difference, uint64_t pkt_offset, uint32_t pkt_length) {
        if (difference >= capacity_) {
            throw std::out_of_range("insert: difference index out of range");
        }
        data_[difference].find(pkt_offset, pkt_length);
    }

    // 判断指定下标的 metarecebuf 是否接收完整
    bool iscomplete(uint8_t difference) const {
        if (difference >= capacity_) {
            throw std::out_of_range("iscomplete: difference index out of range");
        }
        return data_[difference].is_complete();
    }

    // 设置指定下标 metarecebuf 的接收指针
    void set_recv_pointer(uint8_t difference, uint8_t* src) {
        if (difference >= capacity_) {
            throw std::out_of_range("set_recv_pointer: difference index out of range");
        }
        data_[difference].set_src(src);
    }

    // 设置指定下标 metarecebuf 的预期长度
    void rx_len(uint8_t difference, size_t expected) {
        if (difference >= capacity_) {
            throw std::out_of_range("rx_len: difference index out of range");
        }
        data_[difference].addexplen(expected);
    }

    // 判断指定下标的 metarecebuf 是否接收完整
    // 注意：此处 expected 参数未被使用，可根据需要调整逻辑
    bool isreceived(uint8_t difference, size_t expected) const {
        if (difference >= capacity_) {
            throw std::out_of_range("isreceived: difference index out of range");
        }
        return data_[difference].is_complete();
    }

    // 检查给定下标 index 是否在当前有效数据区间内。
    // 如果不在区间内，则通过多次调用 push_back() 推进 tail_，
    // 直至 index 成为有效数据的一部分。
    void indexcheck(uint8_t index) {
        // 如果队列为空，必须先调用一次 push_back() 添加一个元素，
        // 这样才能让有效区间不再为空，从而将 index 纳入有效区间
        if (empty()) {
            size_t desiredTail = (index + 1) % capacity_;
            // 计算需要调用 push_back() 的次数（即从当前 tail_ 推进到 desiredTail 的步数）
            size_t pushes = (desiredTail + capacity_ - tail_) % capacity_;
            for (size_t i = 0; i < pushes; ++i) {
                push_back();
            }
            return;
        }
        
        bool inRange = false;
        if (head_ < tail_) {
            // 无环绕情况：有效区间为 [head_, tail_)
            inRange = (index >= head_ && index < tail_);
        } else {
            // 环绕情况：有效区间为 [head_, capacity_) ∪ [0, tail_)
            inRange = (index >= head_ || index < tail_);
        }
        
        if (!inRange) {
            // 计算期望的 tail_ 值：为了让 index 成为有效数据的一部分，应使 tail_ = (index + 1) % capacity_
            size_t desiredTail = (index + 1) % capacity_;
            // 计算需要调用 push_back() 的次数（即从当前 tail_ 推进到 desiredTail 的步数）
            size_t pushes = (desiredTail + capacity_ - tail_) % capacity_;
            for (size_t i = 0; i < pushes; ++i) {
                push_back();
            }
        }
    }

    ~RCircularQueue() = default;
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

    // Key: sent packet number, value: correspoind offset
    std::unordered_map<uint64_t, uint64_t> pktnum2offset;

    // map for received application pktnum and corresponding offset
    std::vector<uint8_t> receivevector;

    RCset receive_offset;

    std::vector<uint8_t> acknowldge_header;

    struct msghdr acknowldge_msghdr;

    std::vector<struct iovec> acknowldge_iov;  

    size_t send_packet_type = 0;

    std::vector<uint8_t> receive_result;

    /*
    store norm2 for every 256 bits float
    Note: It refers to the priorty of each packet.
    */ 
    std::vector<uint8_t> norm2_vec;

    size_t rx_length;

    bool recv_flag;

    uint64_t send_num;

    bool bidirect;

    Recovery recovery;

    std::array<PktNumSpace, 3> pkt_num_spaces;

    std::chrono::nanoseconds rtt;

    std::chrono::nanoseconds srtt;

    std::chrono::nanoseconds minrtt;

    std::chrono::nanoseconds rto;

    std::chrono::nanoseconds rttvar;
    
    std::chrono::high_resolution_clock::time_point handshake;

    // SendBuf send_buffer;

    RecvBuf rec_buffer;

    size_t current_buffer_pos;

    bool initial;

    // total data sent after one get data.
    size_t written_data_once;

    // Record errno
    size_t dmludp_error;

    // Used to record how many packet has been sent before EAGAIN
    size_t dmludp_error_sent;

    static std::shared_ptr<Connection> connect(sockaddr_storage local, sockaddr_storage peer, Config config) {
        return std::make_shared<Connection>(local, peer, config, false);
    };

    static std::shared_ptr<Connection> accept(sockaddr_storage local, sockaddr_storage peer, Config config)  {
        return std::make_shared<Connection>(local, peer, config, true);
    };

    const uint8_t handshake_header[sizeof(Header)] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    const uint8_t fin_header[sizeof(Header)] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};;

    // when get new data flow, send_connection_difference++
    // WILL BE DROPPED
    uint8_t send_connection_difference;

    /*
    receive_connection_difference keeps current data flow send_connection_difference. 
    If receive_complete() is true, receive_connection_difference++ to keep track with send_connection_difference.
    WILL BE DROPPEP 
    */
    uint8_t receive_connection_difference;

    size_t current_loop_min;

    size_t current_loop_max;

    std::pair<uint64_t, uint64_t> receive_range;

    // size_t send_status;
    size_t send_status_flag;

    /*
    ts_record is used to calculate approximate send ts for each packet.
    Approximate ts ~= (2nd ts - 1st ts)/(2nd pkt - 1st pkt + 1)
    */ 
    std::pair<std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>, std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>> ts_record;

    std::chrono::high_resolution_clock::time_point start_ts;

    std::chrono::high_resolution_clock::time_point end_ts;
    
    size_t normal_initial = 0;

    ssize_t ack_pn = -1;
    // Send message

    /* Replace the conbination of send_msg, send_iov and send_header to reduce packet genaratio cost*/
    std::vector<Message> send_message;

    std::vector<RCMessage> receive_message;
    
    // EAGAIN recovery.
    ssize_t start_index = -1;

    ssize_t end_index = -1;

    uint64_t ACKrange;

    ssize_t min_received;

    ssize_t max_received;

    ssize_t next_received;

    // Set at get_data()
    size_t data_gotten;    

    bool difference_flag;

    size_t receive_upper_bound = 0;

    size_t receive_upper_limit = 0;

    /*Receive buffer*/
    std::vector<uint8_t> rx_buffer;

    // std::vector<std::pair<ssize_t, uint32_t>> rangemap;

    std::vector<uint8_t> receive_available_map;

    TSCircularQueue tsInfo;

    SCircularQueue sendbufferqueue;

    std::deque<std::pair<uint8_t, uint16_t>> zerolist;

    RCircularQueue recvCQ;

    RecordInfo receive_record;

    Connection(sockaddr_storage local, sockaddr_storage peer, Config config, bool server):    
    recv_count(0),
    is_server(server),
    handshake_completed(false),
    handshake_confirmed(false),
    closed(false),
    server(server),
    localaddr(local),
    peeraddr(peer),
    stop_flag(true),
    stop_ack(true),
    norm2_vec(),
    recv_flag(false),
    send_num(0),
    rtt(0),
    srtt(0),
    minrtt(0),
    rto(0),
    rttvar(0),
    handshake(std::chrono::high_resolution_clock::now()),
    bidirect(true),
    initial(false),
    current_buffer_pos(0),
    written_data_once(0),
    dmludp_error(0),
    rx_length(0),
    dmludp_error_sent(0),
    send_connection_difference(0),
    receive_connection_difference(0),
    current_loop_min(0),
    current_loop_max(0),
    recovery(MAX_SEND_UDP_PAYLOAD_SIZE),
    data_gotten(0),
    difference_flag(false),
    send_status_flag(0),
    acknowldge_iov(3),
    // send_buffer(MAX_SEND_UDP_PAYLOAD_SIZE),
    receivevector(3000, 0),
    receive_offset(MAX_SEND_UDP_PAYLOAD_SIZE),
    rx_buffer(MAX_SEND_UDP_PAYLOAD_SIZE * RX_CONST, 0),
    receive_available_map(RX_CONST, 0)
    {
        send_message.resize(ONCE_SEND_LIMIT);

        // receive_message.resize(ONCE_RECEIVE_LIMINT);
        receive_message.resize(RX_CONST);

        receive_offset.add_rule(100 * 1024 * 1024);

        acknowldge_header.resize(sizeof(Header));
        pktnum2offset.reserve(1);
        set_receive_message();
    };

    ~Connection(){};

    void set_receive_message(){
        for (auto i = 0 ; i < receive_message.size(); ++i){
            receive_message[i].set_receive_message(rx_buffer.data() + MAX_SEND_UDP_PAYLOAD_SIZE * i, MAX_SEND_UDP_PAYLOAD_SIZE);
        }
    }


    void initial_rtt() {
        auto arrive_time = std::chrono::high_resolution_clock::now();
        srtt = arrive_time - handshake;
        rttvar = srtt / 2;
        rto = srtt + 4 * rttvar;
    }

    void update_rtt3(std::chrono::high_resolution_clock::time_point send_time, std::chrono::high_resolution_clock::time_point receive_time){
        rtt = receive_time - send_time;
        auto tmp_srtt = std::chrono::duration<double, std::nano>(srtt.count() * alpha + (1 - alpha) * rtt.count());
        srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_srtt);
        auto diff = srtt - rtt;
        auto tmp_rttvar = std::chrono::duration<double, std::nano>((1 - beta) * rttvar.count() + beta * std::abs(diff.count()));
        rttvar = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_rttvar);
        rto = srtt + 4 * rttvar;
    }

    void update_rtt() {
        /*
        handshake_confirmed
        handshake_complete
        
        1st RTO:
        SRTT <- R
        RTTVAR <- R/2
        RTO <- SRTT + max (G, K*RTTVAR)
        where K = 4.

        RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
        SRTT <- (1 - alpha) * SRTT + alpha * R'
        RTO <- SRTT + max (G, K*RTTVAR)
        */
        auto arrive_time = std::chrono::high_resolution_clock::now();
        rtt = arrive_time - handshake;    
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(arrive_time.time_since_epoch()).count();
        auto tmp_srtt = std::chrono::duration<double, std::nano>(srtt.count() * alpha + (1 - alpha) * rtt.count());
        srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_srtt);
        auto diff = srtt - rtt;
        auto tmp_rttvar = std::chrono::duration<double, std::nano>((1 - beta) * rttvar.count() + beta * std::abs(diff.count()));
        rttvar = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_rttvar);
        if (srtt.count() < minrtt.count()){
            minrtt = srtt;
        }
        rto = srtt + 4 * rttvar;
    };

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

    bool recv_slice2(size_t rx_count, size_t receive_max, struct timespec ts = {0, 0}){
        receive_upper_bound = rx_count;
        receive_upper_limit = std::max(receive_upper_limit, receive_max);
        bool send_flag_ = false;
        for (auto i = 0 ; i < rx_count ; i++){
            auto pkt_ty = receive_message[i].get_packet_type();

            if (pkt_ty == Type::ACK){
                process_acknowledge2(i);
                // transmission_complete_check();
            }

            if (pkt_ty == Type::Application){
                process_application_packet2(i);
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

    /*Max received index*/
    size_t boundary(){
        return receive_upper_limit;
    }

    size_t send_data2(){
        auto payload_ = 0;
        if (send_packet_type == Type::ACK){
            payload_ = send_acknowledge();
        }
        return payload_;
    }

    size_t recv_slice(uint8_t* src, size_t src_len, struct timespec ts = {0, 0}){
        recv_count += 1;
       
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
        
        // All side can send data.
        if (pkt_ty == Type::ACK){
            process_acknowledge(src, src_len, ts);
        }

        if (pkt_ty == Type::Application){
            process_application_packet(src, src_len);                 
        }

        if (pkt_ty == Type::Stop){
            stop_flag = false;
            return 0;
        }

        return read_;
    };

    void process_application_packet2(size_t index){
        Packet_num_len pkt_num = receive_message[index].get_packet_number();
        Offset_len pkt_offset = receive_message[index].get_packet_offset();
        Difference_len pkt_difference = receive_message[index].get_packet_difference();
        auto pkt_length = receive_message[index].get_packet_length();

        
        recvCQ.indexcheck(pkt_difference);
        receive_available_map[index] = 1;
        /*TODO(2.24): break index, new parameter: index_check*/

        if (pkt_offset == 0){
            zerolist.push_back(std::make_pair(pkt_difference, index));
        }
     
        if (min_received == -1){
            min_received = pkt_num;
        }
        max_received = pkt_num;

        /* bit map substitude byte map*/
        send_num = pkt_num;
        
        size_t pos = max_received - current_loop_min;
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range." << std::endl;
            return;
        }
        receivevector[byte_index] |= (1 << bit_index);  

        /*TODO: Receive quene to receive*/
        /*
        if(!receivequeue[pkt_difference].receive_offset.find(pkt_offset)){
            receivequeue[pkt_difference].receive_offset.insert(pkt_offset);
            receivequeue[pkt_difference].rec_buffer.reg(pkt_length);
        }
        */
        // if(!receive_offset.find(pkt_offset) && pkt_difference == receive_connection_difference){
        //     receive_offset.insert(pkt_offset);
        //     rec_bufferp[].reg(pkt_length);
        // }
        // if(!receive_offset[pkt_difference].find(pkt_offset)){
        //     receive_offset[pkt_difference].insert(pkt_offset);
        //     rec_buffer[pkt_difference].reg(pkt_length);
        // }
        recvCQ.insert(pkt_difference, pkt_offset, pkt_length);

    };

    bool received(size_t explen_){
        return recvCQ.isreceived(zerolist[0].first, explen_);
    }


    void process_application_packet(uint8_t* src, size_t src_len){
        
    };
    
    size_t send_acknowledge(){
        auto ty = Type::ACK;
 
        Header* hdr = reinterpret_cast<Header *>(acknowldge_header.data());
        hdr->ty = ty;
        hdr->pkt_num = send_num;
        hdr->offset = 0;
        hdr->difference = receive_connection_difference;

        size_t info_len = (max_received - current_loop_min + 1 + 7) / 8;
        hdr->pkt_length = info_len + 16;

        acknowldge_iov[0].iov_base = acknowldge_header.data();
        acknowldge_iov[0].iov_len = sizeof(Header);

        ACKrange = min_received;
        acknowldge_iov[1].iov_base = &ACKrange;
        acknowldge_iov[1].iov_len = sizeof(uint64_t);

        acknowldge_iov[2].iov_base = receivevector.data();
        acknowldge_iov[2].iov_len = info_len;

        acknowldge_msghdr.msg_iov = &acknowldge_iov[0];
        acknowldge_msghdr.msg_iovlen = 3;

        send_packet_type = ty;
        return sizeof(Header) + hdr->pkt_length;
    }
    

    bool check_status(){
        // if (recovery.cwnd_available() && !sendbufferqueue.empty()) return true;
        if (recovery.cwnd_available() && sendbufferqueue.ready()) return true;
        return false;
    }

    void update_receive_parameter(){
        current_loop_min = current_loop_max + 1;
    }

    std::chrono::system_clock::time_point timespecToChrono(const struct timespec& ts) {
        //  tv_sec to chrono second
        auto seconds = std::chrono::seconds(ts.tv_sec);

        //  tv_nsec to chrono nanosecond
        auto nanoseconds = std::chrono::nanoseconds(ts.tv_nsec);

        return std::chrono::system_clock::time_point(seconds + nanoseconds);
    }

    void process_acknowledge(const uint8_t* src, size_t src_len, const struct timespec& ts){

    }

    void process_acknowledge2(const size_t index_){
        auto pkt_num = receive_message[index_].get_packet_number();
        auto pkt_len = receive_message[index_].get_packet_length();

        /*To acknowledge packet receive ts*/
        struct cmsghdr *cmsg;
        struct timespec *ts;
        for (cmsg = CMSG_FIRSTHDR(&receive_message[index_].message_body); cmsg != nullptr; cmsg = CMSG_NXTHDR(&receive_message[index_].message_body, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMPING) {
                ts = (struct timespec *) CMSG_DATA(cmsg);
            }
        }

        // auto sendts = timespecToChrono(*ts);
        auto receivets = std::chrono::high_resolution_clock::now();
        auto ackts = tsInfo.removeBeforeValue(pkt_num);
        update_rtt3(receivets, ackts);

        auto first_pn = *reinterpret_cast<const uint64_t*>(receive_message[index_].iov[1].iov_base);
        auto end_pn = pkt_num;
        bool loss = false;
        size_t total_send = end_pn - first_pn + 1;
        auto ack_src = reinterpret_cast<const uint8_t*>(receive_message[index_].iov[1].iov_base) + sizeof(uint64_t);
        size_t byte_index = 0;
        size_t bit_index = 0;

        auto pn = first_pn;
        for (auto i = sendbufferqueue.start(); i < sendbufferqueue.end(); i = (i + 1) % 256){
            if (pn > end_pn){
                break;
            }
            auto sendpair = sendbufferqueue.data_[i].get_packet_range();
            while(true){
                if (pn >= sendpair.first && pn <= sendpair.second){
                    byte_index = (pn - first_pn) / 8;
                    bit_index = (pn - first_pn) % 8;
                    size_t value = (ack_src[byte_index] >> bit_index) & 1;
                    sendbufferqueue.data_[i].ack4offset(pn, (bool)value);
                    pn++;
                }else{
                    sendbufferqueue.data_[i].reset_packet_range();
                    break;
                }
            }
        }
        ack_pn = end_pn;
        
        if (loss){
            recovery.check_point();
            // recovery.congestion_event(timespecToChrono(*ts));
            // recovery.on_packet_ack(total_send, timespecToChrono(*ts), std::chrono::duration_cast<std::chrono::seconds>(minrtt));
            recovery.congestion_event(receivets);
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
        }else{
            // recovery.on_packet_ack(total_send, timespecToChrono(*ts), std::chrono::duration_cast<std::chrono::seconds>(minrtt));
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
        }

    }


    void clear_recv_setting(){
        receive_offset.clear();
    }

    void recv_reset(){
        rec_buffer.reset();
    }

    void update_receive_difference(){
        receive_connection_difference += 1;
    }

     bool receive_complete(){
        if (receive_connection_difference == recvCQ.start()){
            return recvCQ.iscomplete(receive_connection_difference);
        }
        return false;
    }

    bool zerocheck(){
        if(receive_connection_difference == zerolist[0].first){
            return true;
        }
        return false;
    }

    void rx_len(size_t expected){
        recvCQ.rx_len(zerolist[0].first, expected);
    }

    void reset_rx_len(){
        rx_length = 0;
    }

    void set_send_time(){
        handshake = std::chrono::high_resolution_clock::now();
    }
    

    bool get_data(struct iovec* iovecs, int iovecs_len, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
        /*
        triger data preparation
        */
        bool completed = true;
        written_data_once = 0;
	    dmludp_error_sent = 0;

        current_buffer_pos = 0;

        if (sendbufferqueue.full()){
            return false;
        }

        sendbufferqueue.push_back(iovecs, iovecs_len, priotity_list);
        
        recovery.bytes_in_flight = 0;
        set_handshake();
        pktnum2offset.clear();
        // send_buffer.clear();
        data_gotten = 0;
        return completed;
    }

    size_t get_once_data_len(){
        return written_data_once;
    }

    void clear_sent_once(){
        written_data_once = 0;
    }

    ssize_t prepareData() {
        Type ty = Type::Application;
        ssize_t out_len = 0; 
        Offset_len out_off = 0;
        size_t i = 0;
        ssize_t tramssioning_index = -1;

        ssize_t cwnd_limit = (recovery.cwnd_available() + MAX_SEND_UDP_PAYLOAD_SIZE - 1) / MAX_SEND_UDP_PAYLOAD_SIZE;
        if (cwnd_limit <= 0){
            return 0;
        }

        size_t sent_limit = std::min(send_message.size(), (size_t)cwnd_limit);
        ssize_t sent = 0;
        for (i = sendbufferqueue.start() ; i < sendbufferqueue.end() ; i = (i + 1)%sendbufferqueue.max()) {
            while (true){
                size_t send_status = 0;
                auto s_flag = sendbufferqueue.data_[i].metabuf.emit(send_message[sent].iov[1], out_len, out_off, send_status);
                std::cout<<"[Debug] out_len:"<<out_len<<", out_off:"<<out_off<<std::endl;
                if (out_len == -1) {
                    break;
                }

                auto pn = pkt_num_spaces[0].updatepktnum();
                send_message[sent].setMessageHeader(pn, out_off, i, (Packet_num_len)out_len);
                recovery.on_packet_sent(out_len);
                // if (sendbufferqueue.data_[i].metabuf.meta_status == MetaFlag::Initial){
                //     sendbufferqueue.data_[i].add_transmission(pn, out_off);
                // }else if(sendbufferqueue.data_[i].metabuf.meta_status == MetaFlag::Retransmission){
                //     sendbufferqueue.data_[i].add_retransmission(pn, out_off);
                // }
                if (send_status == 1){
                    sendbufferqueue.data_[i].add_transmission(pn, out_off);
                }else if(send_status == 0){
                    sendbufferqueue.data_[i].add_retransmission(pn, out_off);
                }
                sent++;
                if (sent >= sent_limit){
                    /*TODO add pakcet number-offset mapping*/
                    break;
                }
            }
        }

        return sent;
    }

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

    void send_packet_complete(size_t err_ = 0, size_t sent = 0, const std::chrono::system_clock::time_point& start_ts = std::chrono::system_clock::time_point{}){
        if(send_packet_type == 0){
            return;
        }
        set_error2(err_);
        if (err_ != 0){
            if (send_packet_type == Type::Application){
                end_ts = std::chrono::high_resolution_clock::now();
                tsInfo.updateQueue(send_message[start_index].get_packet_number(), start_ts, pkt_num_spaces[0].getpktnum(), end_ts);
                start_index = start_index + sent;
            }
            return;
        }
        
        if(send_packet_type == Type::ACK){
            for (size_t i = 0; i < receivevector.size(); ++i) {
                receivevector[i] = 0;
            }
            min_received = -1;
            current_loop_min = max_received + 1;
            process_application_copy();
            /*
            For cache in receiver_cache
                cache.copy()
            */
        }else if(send_packet_type == Type::Application){
            end_index = -1;
            set_handshake();
            tsInfo.updateQueue(send_message[start_index].get_packet_number(), start_ts, pkt_num_spaces[0].getpktnum(), end_ts);
        }else if(send_packet_type == Type::Stop){

        }else if(send_packet_type == Type::Fin){

        }

        send_packet_type = 0;
    }

    ssize_t get_start(){
        return next_available(-1);
    }

    size_t get_end(){
        return receive_message.size();
    }

    void get_recv_target(uint8_t * target_){
        /*check top poiner is null or not*/
        recvCQ.set_recv_pointer(zerolist[0].first, target_);
    }

    ssize_t next_available(ssize_t index){
        auto idx = index + 1;
        while(true){
            if (receive_available_map[idx] == 0){
                break;
            }
            idx++;
        }

        return idx;
    }


    void process_application_copy(){
        Offset_len pkt_offset;
        Packet_len pkt_len;
        Difference_len pkt_difference;
        if (receive_connection_difference == zerolist[0].first && recvCQ.data_[receive_connection_difference].srcset == 1){
            auto index = zerolist[0].second;
            pkt_offset = receive_message[index].get_packet_offset();
            pkt_difference = receive_message[index].get_packet_difference();
            memcpy(recvCQ.data_[pkt_difference].metabuf.src, reinterpret_cast<uint8_t*>(receive_message[index].iov[1].iov_base), 48);
            receive_available_map[index] = 0;
            return;
        }
        
        /*TODO: record ahead index consumption to reduce iterations*/
        for (auto index = 0; index < boundary(); index++){
            if (receive_available_map[index] == 0){
                if (!receive_record.empty()){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    if (pkt_offset >= 48){
                        memcpy(reinterpret_cast<uint8_t*>(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48), reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }else{
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }
                    receive_record.reset();
                }   
                continue;
            }

            if (receive_connection_difference == pkt_difference && recvCQ.data_[pkt_difference].metabuf.src != nullptr){
                pkt_offset = receive_message[index].get_packet_offset();
                pkt_len = receive_message[index].get_packet_length();

                receive_available_map[index] = 0;

                if(receive_record.get_end_index() - receive_record.get_start_index() == 7){ 
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    if (pkt_offset >= 48){
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }else{
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }
                    receive_record.set(index, pkt_len, pkt_offset);
                    continue;
                }

                if (receive_record.get_target_offset() != pkt_offset){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    if (pkt_offset >= 48){
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }else{
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }
                    receive_record.set(index, pkt_len, pkt_offset);
                    continue;
                }

                receive_record.update(index, pkt_offset, pkt_len);
            }else{
                if (!receive_record.empty()){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    if (pkt_offset >= 48){
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }else{
                        memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
                    }
                    receive_record.reset();
                }   
            }
        }


        if (!receive_record.empty()){
            auto copy_len = receive_record.get_acumulation();
            auto copy_index = receive_record.get_start_index();
            if (pkt_offset >= 48){
                memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
            }else{
                memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
            }
            receive_record.reset();
        }   

        /*
        1. Max 8 copy packets
        2. index contious breaks
        3. difference differ
        */
        /*
        if receive_connection_difference == pkt_difference & src
            if (end - start == 7)
                !receive_record.empty
                    copy(recordinfo)
                    receive_record.set(index, pkt_len)
                continue;
            
            if (receive_record.offset + packet_limit != pkt_offset)
                if !receive_record.empty
                    copy(record)
                continue;
            else
                receive_record.update(end_index, pkt_len);
        else
            if check receive_record is not empty; other block data interupt
                copy()
                reset()
            else
                continue;
        
        if(!receive_record.empty())
            copy(receive_record)
            receive_record.reset;
        */
       
    }


    void set_send_status(int status_){
        send_status_flag = status_;
    }

    size_t get_send_status(){
        return send_status_flag;
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
        if (sendbufferqueue.data_[sendbufferqueue.start()].iscomplete()){
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
    // size_t send_data(msghdr)
    size_t send_data(uint8_t* out){
        size_t total_len = HEADER_LENGTH;

        uint64_t psize = 0;

        /*
        If there exists EAGAIN, resend message first

        return
        */

        auto ty = write_pkt_type(); 

        if (ty == Type::Handshake && server){
            // out = handshake_header;
            memcpy(out, handshake_header, HEADER_LENGTH);
            set_handshake();
        }

        if (ty == Type::Handshake && !server){
            // out = handshake_header;
            memcpy(out, handshake_header, HEADER_LENGTH);
            initial = true;
        }

        if (ty == Type::ACK){
            /*
            send_acknowledge(src, size(src));
            */
            // If acknowledge too long, just 1000 acknowledge lastest part.
            if(receive_result.size() > 1200){
                psize = 1200 + 2 * sizeof(uint64_t);
            }else{
                psize = (uint64_t)(receive_result.size()) + 2 * sizeof(uint64_t);
            }
            
            Header* hdr = new Header(ty, send_num, 0, receive_connection_difference, psize);
            if (difference_flag){
                uint8_t tmp = receive_connection_difference - 1;
                hdr->difference = tmp;
            }

            // Recevie info clear.

            memcpy(out, hdr, HEADER_LENGTH);
            if(receive_range.second - receive_range.first <= 1199){
                memcpy(out + HEADER_LENGTH, &receive_range.first, sizeof(uint64_t));
                memcpy(out + HEADER_LENGTH + sizeof(uint64_t), &receive_range.second, sizeof(uint64_t));
                memcpy(out + HEADER_LENGTH + 2 * sizeof(uint64_t), receive_result.data(), receive_result.size());
            }else{
                // memcpy(out + HEADER_LENGTH, &receive_range.first, sizeof(uint64_t));
                auto first_pn = receive_range.second - 1199;
                auto range_offset = first_pn - receive_range.first;
                memcpy(out + HEADER_LENGTH, &first_pn, sizeof(uint64_t));
                memcpy(out + HEADER_LENGTH + sizeof(uint64_t), &receive_range.second, sizeof(uint64_t));
                memcpy(out + HEADER_LENGTH + 2 * sizeof(uint64_t), receive_result.data() + range_offset, 1200);
            }
            
            receive_result.clear();
            difference_flag = false;
            delete hdr; 
            hdr = nullptr; 
            update_receive_parameter();
        }      


        // if (ty == Type::Fin){
        //     memcpy(out, fin_header, HEADER_LENGTH);
        //     return total_len;
        // }

        total_len += (size_t)psize;

        return total_len;
    };


    size_t send_data_stop(uint8_t* out){ 
        size_t total_len = HEADER_LENGTH;

        auto pn = pkt_num_spaces[1].updatepktnum();

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

    // Check if fixed length of first entry in received buffer exist.
    // bool check_first_entry(size_t check_len){
    //     auto fst_len = rec_buffer.first_item_len(check_len);
    //     if (fst_len != check_len){
    //         return false;
    //     }
    //     return true;
    // };

    // void recv_padding(size_t total_len){
    //     rec_buffer.data_padding(total_len);
    // }

    size_t read(uint8_t* out, bool iscopy, size_t output_len = 0){
        return rec_buffer.emit(out, iscopy, output_len);
    };


    // bool has_recv(){
    //     return rec_buffer.is_empty();
    // }
    
    // size_t recv_len(){
    //     return rec_buffer.length();
    // }

    uint8_t priority_calculation(uint64_t off){
        auto real_index = (uint64_t)(off / MAX_SEND_UDP_PAYLOAD_SIZE);
        if (real_index >= norm2_vec.size()){
            std::cout<<"out of range"<<std::endl;
        }
        return norm2_vec[real_index];
    };

    void reset(){
        norm2_vec.clear();
        // send_buffer.clear();
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
    ///
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

        if (recv_flag == true){
            recv_flag = false;
            return Type::ACK;
        }

        if (rtt.count() != 0){
            return Type::Application;
        }

        return Type::Unknown;
    };
    
};

}