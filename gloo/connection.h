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
#include <typeinfo>
#include <dlfcn.h>
#include <cassert>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <optional>

namespace dmludp {

const size_t HEADER_LENGTH = sizeof(Header);

// The default max_datagram_size used in congestion control.
const size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1440;

const size_t MAX_ACK_UDP_PAYLOAD_SIZE = 1400;

const size_t RX_CONST = 4000;

const size_t ONCE_LIMIT = 1300;

const size_t ONCE_SEND_LIMIT = ONCE_LIMIT;

const size_t ONCE_RECEIVE_LIMINT = ONCE_LIMIT;

const double alpha = 0.875;

const double beta = 0.25;

const size_t DataBlock = 16;

const size_t MapSetLimit = 20;

const size_t ReTransmissionMapLimit = 2000;

const size_t LIMIT_SIZE_T = std::numeric_limits<size_t>::max();

const uint64_t LIMIT_UINT64_T = std::numeric_limits<uint64_t>::max();

const uint32_t LIMIT_UINT32_T = std::numeric_limits<uint32_t>::max();

const uint16_t LIMIT_UINT16_T = std::numeric_limits<uint16_t>::max();

const uint8_t LIMIT_UINT8_T = std::numeric_limits<uint8_t>::max();

using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Offset_len = uint64_t;

using Difference_len = uint32_t;

using Packet_len = uint16_t;

/*a is latter received, b is former received*/
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
is_newer(T a, T b) {
    using SignedT = typename std::make_signed<T>::type;
    return static_cast<SignedT>(a - b) > 0;
}

class Message{
    public:
        struct msghdr message_body;

        iovec iov[2];

        Header message_header;

        Message(){
            iov[0].iov_base = static_cast<void*>(&message_header);
            iov[0].iov_len = sizeof(Header);

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

        void setMessageHeader(Packet_num_len pn, Offset_len offset, Difference_len difference, Packet_len length) {
            message_header.pkt_num = pn;
            message_header.offset = offset;
            message_header.difference = difference;
            message_header.pkt_length = (Packet_num_len)length;
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


        msghdr* getMessageHeader() {
            return &message_body;
        }
};

class RCMessage : public Message {
    private:
        // bool use_status = true;
    public:
        RCMessage(){}
            
        void set_receive_message(void *ptr, size_t ptr_len){
            iov[1].iov_base = ptr;
            iov[1].iov_len = ptr_len;
        }

        ~RCMessage(){};
};

/*Send timestamp queue*/
class TSCircularQueue{
private:
    /*first packet number + timestamp, last packet number + timestamp*/
    using DataType = std::pair<std::pair<uint64_t, std::chrono::steady_clock::time_point>,
                               std::pair<uint64_t, std::chrono::steady_clock::time_point>>;
    using TimeStamp = std::chrono::steady_clock::time_point;
    std::vector<DataType> buffer; 
    size_t head;                  
    size_t tail;                 
    size_t capacity;              
    size_t count;                 

public:
    explicit TSCircularQueue(size_t capacity = 100)
        : buffer(capacity), head(0), tail(0), capacity(capacity), count(0) {}

    ~TSCircularQueue(){}

    void enqueue(const DataType& value) {
        if (isFull()) {
            throw std::overflow_error("TSCircularQueue is full(enqueue)");
        }
        // std::cout<<"1 enqueue:"<<count<<std::endl;
        buffer[tail] = value;
        tail = (tail + 1) % capacity;
        ++count;
        // std::cout<<"enqueue:("<<value.first.first<<", "<<value.second.first<<") "<<count<<std::endl;
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

    size_t size() {
        return count;
    }

    void clear() {
        head = 0;
        tail = 0;
        count = 0;
    }


    std::optional<TimeStamp> removeBeforeValue(uint64_t value) {
        if (isEmpty()) {
            throw std::underflow_error("TSCircularQueue is empty(remove)");
        }
        // std::cout << "start removeBeforeValue:" << value << std::endl;

        TimeStamp result;

        bool has = false;
        while (size() > 0) {
            const auto& item = front();  
            if (item.second.first < value){
                // std::cout<<"1 removeBeforeValue:"<< value<<", "<< item.first.first<<", "<<item.second.first<<", "<<count<<std::endl;
                dequeue();
            }

            if (item.first.first <= value && item.second.first >= value){
                // std::cout<<"3 removeBeforeValue:"<< value<<", "<< item.first.first<<", "<<item.second.first<<", "<<count<<std::endl;
                if (item.first.first == item.second.first){
                    result = item.first.second;
                }else{
                    result = item.first.second + (item.second.second - item.first.second) * (value - item.first.first) / (item.second.first - item.first.first) ; 
                }
                has = true;
                break;
            }

            if (value < item.first.first){
                // std::cout<<"2 removeBeforeValue:"<< value<<", "<< item.first.first<<", "<<item.second.first<<", "<<count<<std::endl;
                break;
            }
           
        }

        if(has){
            return result;
        }else{
            return std::nullopt;
        }
    }

    DataType& at(size_t i) {
        if (i >= count)
            throw std::out_of_range("Index out of range");
        return buffer[(head + i) % capacity];
    }

    size_t unused_size() const {
        return capacity - count;
    }

    DataType& at_unused(size_t i) {
        if (i >= unused_size())
            throw std::out_of_range("Unused index out of range");
        return buffer[(tail + i) % capacity];
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
    private:
        DynamicBitset RCset_body;

        size_t payload_len = MAX_SEND_UDP_PAYLOAD_SIZE;
    public:
        RCset(size_t capacity_ = 330000): 
        RCset_body(capacity_){}

        size_t count() const{
            return RCset_body.count();
        }

        bool find(Offset_len offset_){
            auto index = get_index(offset_);
            if (index >= RCset_body.size()){
                std::cerr << "index:" << index << ", RCset_body.size:" << RCset_body.size() << std::endl;
                throw std::underflow_error("[RCset]: find index beyond capacity_");
            }
            // std::cout<<"find:"<<offset_<<", "<<index<<", "<<RCset_body[index]<<std::endl;
            return RCset_body[index] == 1;
        }

        void insert(Offset_len offset_){
            auto index = get_index(offset_);
            if (index >= RCset_body.size()){
                throw std::underflow_error("[RCset]: insert index beyond capacity_");
            }
            RCset_body[index] = 1;
            // std::cout<<"insert:"<<offset_<<", "<<index<<", "<<RCset_body[index]<<std::endl;
        }

        size_t get_index(Offset_len offset_){
            size_t index;
            if (offset_ < 48){
                index = 0;
            }else{
                index = round_up((offset_ - 48), payload_len) + 1;
            }
            return index;
        }

        size_t round_up(Offset_len a, Offset_len b){
            if (b == 0) {
                throw std::invalid_argument("Division by zero is not allowed");
            }

            return (a + b - 1) / b;
        }


        void clear(){
            RCset_body.clear();
        }

        ~RCset(){}
};

/*Used to record retransmission data*/
class ReTransmissionMap{
    private:
        Packet_num_len start_packet = LIMIT_UINT64_T;

        Packet_num_len end_packet = LIMIT_UINT64_T;

        std::vector<Offset_len> offsets;

        bool use_flag = false;

    public:
        ReTransmissionMap():offsets(ReTransmissionMapLimit, 0){
        };

        ~ReTransmissionMap(){};

        void clear(){
            use_flag = false;
            // start_packet = end_packet = LIMIT_UINT64_T;
            // std::fill(offsets.begin(), offsets.end(), 0);
        }

        void reset(){
            start_packet = end_packet = LIMIT_UINT64_T;
            std::fill(offsets.begin(), offsets.end(), 0);
        }

        Offset_len get_offset(Packet_num_len packetnum){
            if (packetnum < start_packet || packetnum > end_packet){
                return LIMIT_UINT64_T;
            }
            return offsets[packetnum - start_packet];
        }

        void add(Packet_num_len packetnum, Offset_len packetoffset){
            if(start_packet == LIMIT_UINT64_T){
                start_packet = packetnum;
            }

            if (end_packet == LIMIT_UINT64_T){
                end_packet = packetnum;
            }else{
                if (end_packet + 1 == packetnum){
                    end_packet = packetnum;
                }else{
                    std::cerr << "ReTransmissionMap add error: start_packet(" << start_packet 
                    << "), (" << end_packet << "), (" << packetnum << ")" << std::endl;
                    _Exit(0);
                }
            }
            
            if ((end_packet - start_packet) > (offsets.size() - 1)){
                std::cerr << "ReTransmissionMap add() out of boundary(" << start_packet << ", " << end_packet << ")" << std::endl;
                _Exit(0);
            }   
            offsets[end_packet - start_packet] = packetoffset;
            use_flag = true;
        }

        bool empty(){
            if (start_packet == LIMIT_UINT64_T && end_packet != LIMIT_UINT64_T) {
                throw std::underflow_error("Error: start == MAX, end != MAX");
            }
            return ((start_packet == end_packet) && (start_packet == LIMIT_UINT64_T));
        }

        bool inrange(Packet_num_len PacketNum){
            return (PacketNum >= start_packet && PacketNum <= end_packet);
        }

        bool full(){
            return (end_packet - start_packet) == (offsets.size() - 1);
        }

        std::pair<Packet_num_len, Packet_num_len> get_range(){
            return std::make_pair(start_packet, end_packet);
        }

        void print_log(){}
};

/*Used to record packet transmission.*/
class TransmissionMap{
    private:
        std::pair<Packet_num_len, Offset_len> startmap;
        
        std::pair<Packet_num_len, Offset_len> endmap;

        bool use_flag = false;

    public:
        TransmissionMap() : startmap(LIMIT_UINT64_T, 0), endmap(LIMIT_UINT64_T, 0){};

        ~TransmissionMap(){};

        void add(Packet_num_len packetnum, Offset_len packetoffset){
            if(startmap.first == LIMIT_UINT64_T){
                startmap = std::make_pair(packetnum, packetoffset);
            }
   
            if ((endmap.first + 1) == packetnum || endmap.first == LIMIT_UINT64_T){
                if((endmap.first + 1) == packetnum){
                    if(endmap.second + MAX_SEND_UDP_PAYLOAD_SIZE != packetoffset && endmap.second != 0){
                        std::cout<<"[Error] endmap.second:"<<endmap.second<<", "<<packetoffset<<std::endl;
                       _Exit(0);
                    }
                }
                endmap = std::make_pair(packetnum, packetoffset);
            }else{
                throw std::underflow_error("Error: TransmissionMap lacks enough space");
            }      
            use_flag = true;
            // std::cout<<"startmap:"<<startmap.first<<", "<<startmap.second<<", endmap:"<<endmap.first<<", "<<endmap.second<<", "<<packetnum<<", packetoffset:"<<packetoffset<<std::endl;
        }

        bool empty() {
            if (startmap.first == LIMIT_UINT64_T && endmap.first != LIMIT_UINT64_T) {
                throw std::underflow_error("Error: start == MAX, end != MAX");
            }
            return ((startmap.first == endmap.first) && (endmap.first == LIMIT_UINT64_T));
        }  

        void clear(){
            use_flag = false;
            // startmap = endmap = std::make_pair(LIMIT_UINT64_T, 0);
        }

        void reset(){
            startmap = endmap = std::make_pair(LIMIT_UINT64_T, 0);
        }

        std::pair<Packet_num_len, Packet_num_len> get_range(){
            return std::make_pair(startmap.first, endmap.first);
        }

        Offset_len get_offset(Packet_num_len packetnum_){
            Offset_len offset_ = LIMIT_UINT64_T;
            // std::cout<<"startmap:"<<startmap.first<<", "<<startmap.second<<", endmap:"<<endmap.first<<", "<<endmap.second<<std::endl;
            if (packetnum_ < startmap.first || packetnum_ > endmap.first){
                return offset_;
            }
            if (startmap.second == 0){
                if (packetnum_ == startmap.first){
                    offset_ = startmap.second;
                }else{
                    offset_ = 48 + (packetnum_ - startmap.first - 1) * MAX_SEND_UDP_PAYLOAD_SIZE;
                }
            }else{
                if (packetnum_ > endmap.first){
                    std::cout<<"packetnum_("<<packetnum_<<") > endmap.first("<<endmap.first<<")"<<std::endl;
                    _Exit(0);
                }
                offset_ = (packetnum_ - startmap.first) * MAX_SEND_UDP_PAYLOAD_SIZE + startmap.second;
            }
            return offset_;
        }

        bool full(){
            return false;
            return (endmap.first - startmap.first) == (ReTransmissionMapLimit - 1);
        }

        bool inrange(Packet_num_len PacketNum){
            return (PacketNum >= startmap.first && PacketNum <= endmap.first);
        }

        void print_log(){
            std::cout << startmap.first<<", " << startmap.second << ", " << endmap.first << ", "<< endmap.second << std::endl;
        }
};

template<typename T, typename = typename std::enable_if<
    std::is_same<T, TransmissionMap>::value || std::is_same<T, ReTransmissionMap>::value>::type>
class MapSet {
    private:
        std::vector<T> buffer_;
        size_t head_;
        size_t tail_;
        size_t capacity_;
        size_t count_;

        void expand_capacity() {
            size_t new_capacity = capacity_ * 2;
            std::vector<T> new_buffer(new_capacity);

            // Re-arrange elements from old buffer to new buffer
            size_t idx = head_;
            for (size_t i = 0; i < count_; ++i) {
                new_buffer[i] = std::move(buffer_[idx]);
                idx = (idx + 1) % capacity_;
            }

            buffer_ = std::move(new_buffer);
            capacity_ = new_capacity;
            head_ = 0;
            tail_ = count_;
        }

    public:
        explicit MapSet(size_t capacity = MapSetLimit)
            : buffer_(capacity), capacity_(capacity),
            head_(0), tail_(0), count_(0) {}

        bool empty() const {
            return count_ == 0;
        }

        bool full() const {
            return count_ == capacity_;
        }

        size_t size() const {
            return count_;
        }

        size_t capacity() const {
            return capacity_;
        }

        size_t used() const {
            return count_;
        }

        void push() {
            if (full()) {
                expand_capacity();
            }
            buffer_[tail_].reset();
            // buffer_[tail_].clear();
            tail_ = (tail_ + 1) % capacity_;
            ++count_;
        }

        void pop() {
            if (empty()) {
                return;
            }
            buffer_[head_].clear();
            head_ = (head_ + 1) % capacity_;
            --count_;
        }

        T* front() {
            if (empty()) {
                return nullptr;
            }
            return &buffer_[head_];
        }

        const T* front() const {
            if (empty()) {
                return nullptr;
            }
            return &buffer_[head_];
        }

        T* back() {
            if (empty()) {
                return nullptr;
            }
            return &buffer_[(tail_ + capacity_ - 1) % capacity_];
        }

        const T* back() const {
            if (empty()) {
                return nullptr;
            }
            return &buffer_[(tail_ + capacity_ - 1) % capacity_];
        }

        void add(Packet_num_len packetnum_, Offset_len packetoffset_) {
            auto backmap = back();
            if (backmap == nullptr) {
                push();
                auto newmap = back();
                newmap->add(packetnum_, packetoffset_);
                return;
            }

            auto maprange = backmap->get_range();
            if ((packetnum_ == maprange.second + 1) && !backmap->full()) {
                backmap->add(packetnum_, packetoffset_);
            } else {
                push();
                auto newmap = back();
                newmap->add(packetnum_, packetoffset_);
            }
        }

        std::pair<Packet_num_len, Packet_num_len> get_range() {
            if (empty()){
                return std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T);
            }
            return buffer_[head_].get_range();
        }

        void removeBeforeValue(Packet_num_len packet_) {
            if (!empty()){
                for (auto i = 0; i < count_; i++){
                    auto range = get_range();
                    if (range.second < packet_ && range.second != LIMIT_UINT64_T) {
                        pop();
                    } else {
                        return;
                    }
                }
            }
        }

        void for_each(const std::function<void(const T&)>& func) const {
            size_t idx = head_;
            for (size_t i = 0; i < count_; ++i) {
                func(buffer_[idx]);
                idx = (idx + 1) % capacity_;
            }
        }

        void clear() {
            for (auto &e: buffer_){
                // e.clear();
                e.reset();
            }
            head_ = 0;
            tail_ = 0;
            count_ = 0;
        }

        Offset_len get_offset(Packet_num_len PacketNum) {
            size_t idx = head_;
            for (size_t i = 0; i < count_; ++i) {
                Offset_len result_offset = buffer_[idx].get_offset(PacketNum);
                if (result_offset != LIMIT_UINT64_T) {
                    if (i != 0) {
                        removeBeforeValue(PacketNum);
                    }
                    return result_offset;
                }
                idx = (idx + 1) % capacity_;
            }
            return LIMIT_UINT64_T;
        }

        T& at(size_t i) {
            if (i >= count_)
                throw std::out_of_range("Index out of range");
            return buffer_[(head_ + i) % capacity_];
        }

        size_t unused_size() const {
            return capacity_ - count_;
        }

        T& at_unused(size_t i) {
            if (i >= unused_size())
                throw std::out_of_range("Unused index out of range");
            return buffer_[(tail_ + i) % capacity_];
        }

        // T& at_unused_reverse(size_t i) {
        //     if (i >= unused_size() )
        //         throw std::out_of_range("Unused reverse index out of range");
        //     size_t index = (tail_ + capacity_ - 1 - i) % capacity_;
        //     return buffer_[index];
        // }

};

class MetaInfo{
    public:
        SendBuf metabuf;

        const Difference_len MetaDifference;

        MapSet<ReTransmissionMap> retransmission_map;

        MapSet<TransmissionMap> transmission_map;

        std::vector<uint64_t> priority_offset;

        uint64_t difference_flag = LIMIT_UINT64_T;

        size_t range_len;

        size_t block_type = std::numeric_limits<size_t>::max();

        /* true is complete, false is not complete*/
        bool send_status = false;

        MetaInfo(size_t difference_flag_ = std::numeric_limits<uint16_t>::max()): 
        MetaDifference(difference_flag_),
        metabuf(MAX_SEND_UDP_PAYLOAD_SIZE),
        range_len(0){};

        ~MetaInfo(){};

        void add_transmission(Packet_num_len packetnum_, Offset_len packetoffset_){
            transmission_map.add(packetnum_, packetoffset_);
        }

        void add_retransmission(Packet_num_len packetnum_, Offset_len packetoffset_){
            retransmission_map.add(packetnum_, packetoffset_);
        }

        void removeoldmap(Packet_num_len packet_){
            retransmission_map.removeBeforeValue(packet_);
            transmission_map.removeBeforeValue(packet_);
        }

        bool no_overlap(const std::pair<Packet_num_len, Packet_num_len>& p1, const std::pair<Packet_num_len, Packet_num_len>& p2) {
            return p1.second < p2.first || p2.second < p1.first;
        }

        std::pair<Packet_num_len, Packet_num_len> get_packet_range(Packet_num_len packet_){
            /*Make sure not old map exists*/
            removeoldmap(packet_);
            auto transmission_front = transmission_map.get_range();
            auto retransmission_front = retransmission_map.get_range();
            if (transmission_front == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T) && retransmission_front == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
                return transmission_front;
            }
            if (!no_overlap(transmission_front, retransmission_front)){
                std::cerr << "Overlap: (" << transmission_front.first << ", " << transmission_front.second << "), ("
                    << retransmission_front.first << ", " << retransmission_front.second << ")" << std::endl;
                _Exit(0);
            }
            if (transmission_front.second < retransmission_front.first){
                return transmission_front;
            }else{
                return retransmission_front;
            }
        }

        std::pair<Packet_num_len, Packet_num_len> get_used_range(Packet_num_len packet_){
            /*Make sure not old map exists*/
            removeoldmap(packet_);
            auto transmission_front = transmission_map.get_range();
            auto retransmission_front = retransmission_map.get_range();

            if (packet_ >= transmission_front.first && packet_ <= transmission_front.second){
                return transmission_front;
            }

            if (packet_ >= retransmission_front.first && packet_ <= retransmission_front.second){
                return retransmission_front;
            }

            return std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T);
        }

        std::tuple<Packet_num_len, Packet_num_len, size_t, bool> get_unused_range(Packet_num_len packet_){
            for (size_t i = 0; i < transmission_map.unused_size(); i++){
                auto result = transmission_map.at_unused(i).get_range();
                // if (result.first != LIMIT_UINT64_T && result.second != LIMIT_UINT64_T){
                //     std::cout<<i<<" transmission_map:" <<  result.first << ", " << result.second << std::endl;
                // }
                if (packet_ >= result.first && packet_ <= result.second){
                    return std::make_tuple(result.first,result.second, i, true);
                }
            }
            for (auto i = 0; i < retransmission_map.unused_size(); i++){
                auto result = retransmission_map.at_unused(i).get_range();
                // if (result.first != LIMIT_UINT64_T && result.second != LIMIT_UINT64_T){
                //     std::cout<<i<<" retransmission_map:" <<  result.first << ", " << result.second << std::endl;
                // }
                if (packet_ >= result.first && packet_ <= result.second){
                    return std::make_tuple(result.first,result.second, i, false);
                }
            }
            return std::make_tuple(LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false);
        }
        

        std::tuple<Packet_num_len, Packet_num_len, size_t, bool> get_cleared_packet_range(Packet_num_len packet_){
            for (size_t i = 0; i < transmission_map.unused_size(); i++){
                auto result = transmission_map.at_unused(i).get_range();
                // if (result.first != LIMIT_UINT64_T && result.second != LIMIT_UINT64_T){
                //     std::cout<<i<<" transmission_map:" <<  result.first << ", " << result.second << std::endl;
                // }
                if (packet_ >= result.first && packet_ <= result.second){
                    return std::make_tuple(result.first,result.second, i, true);
                }
            }
            for (auto i = 0; i < retransmission_map.unused_size(); i++){
                auto result = retransmission_map.at_unused(i).get_range();
                // if (result.first != LIMIT_UINT64_T && result.second != LIMIT_UINT64_T){
                //     std::cout<<i<<" retransmission_map:" <<  result.first << ", " << result.second << std::endl;
                // }
                if (packet_ >= result.first && packet_ <= result.second){
                    return std::make_tuple(result.first,result.second, i, false);
                }
            }
            return std::make_tuple(LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false);
        }

        size_t offset_calculate(Packet_num_len PacketNum) {
            auto result = transmission_map.get_offset(PacketNum);
            if (result != LIMIT_UINT64_T){
                // std::cout<<(int)MetaDifference<<", PacketNum:"<<PacketNum<<", "<<result;
                return result;
            }
            result = retransmission_map.get_offset(PacketNum);
            if (result != LIMIT_UINT64_T){
                return result;
            }
            throw std::underflow_error("Error: packet doesn't belong to this block.");
        }


        void ack4offset(Packet_num_len PacketNum, bool isReceived, Offset_len offset_ = 0){
            auto packet_offset_ = offset_calculate(PacketNum);
            /*Add priority calculation to logit remove "complete" data*/
            if (isReceived){
                // std::cout<<"ack4offset:"<<PacketNum<<", "<<packet_offset_<<std::endl;
                metabuf.acknowledege_and_drop(packet_offset_, true);
            }else{
                metabuf.acknowledege_and_drop(packet_offset_, false);
            }
        }

        void ack4offset2(Packet_num_len PacketNum, size_t mapindex_, bool maptype_, bool isReceived){
            Offset_len packet_offset_;
            if (maptype_ == true){
                packet_offset_ = transmission_map.at_unused(mapindex_).get_offset(PacketNum);
            }else{
                packet_offset_ = retransmission_map.at_unused(mapindex_).get_offset(PacketNum);
            }
            if (isReceived){
                // std::cout<<"ack4offset2:"<<PacketNum<<", "<<packet_offset_<<std::endl;
                metabuf.acknowledege_and_drop(packet_offset_, true);
            }else{
                metabuf.acknowledege_and_drop(packet_offset_, false);
            }
        }

        Difference_len get_difference(){
            return difference_flag;
        }

        void set_buffer(struct iovec* iovecs, int iovecs_len, size_t type_, const Difference_len difference_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
            if (difference_flag != LIMIT_UINT64_T){
                std::cerr << "MetaInfo set_buffer error(difference_flag(" << (int)difference_flag << "), (" << (int)difference_ << "))" << std::endl;
                _Exit(0);
            }
            difference_flag = difference_;
            if ((difference_flag % 16) != MetaDifference){
                std::cerr << "difference_flag(" << (int)difference_flag << "), MetaDifference(" << (int)MetaDifference << ")" << std::endl;
                _Exit(0);
            }
            metabuf.add_Meta(iovecs, iovecs_len);
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
            transmission_map.clear();
            retransmission_map.clear();
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

        void push_back(struct iovec* iovecs, int iovecs_len, int type_, const std::vector<std::vector<uint8_t>> &priotity_list = {}) {
            if (full()){
                std::cerr << "SCircularQueue overflow" << std::endl;
                _Exit(0);
            }
            data_[tail_].set_buffer(iovecs, iovecs_len, type_, lastest_difference, priotity_list);
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

        std::pair<Packet_num_len, Packet_num_len> get_packet_range(Difference_len difference_, Packet_num_len pn_){
            auto index = difference_ % get_capacity();
            return data_[index].get_packet_range(pn_);
        }

        std::tuple<Packet_num_len, Packet_num_len, size_t, bool> get_cleared_packet_range(Difference_len difference_, Packet_num_len pn_){
            auto index = difference_ % get_capacity();
            return data_[index].get_cleared_packet_range(pn_);
        }

        std::pair<Packet_num_len, Packet_num_len> get_used_range(Difference_len difference_, Packet_num_len pn_){
            auto index = difference_ % get_capacity();
            return data_[index].get_used_range(pn_);
        }

        std::tuple<Packet_num_len, Packet_num_len, size_t, bool> get_unused_range(Difference_len difference_, Packet_num_len pn_){
            auto index = difference_ % get_capacity();
            return data_[index].get_unused_range(pn_);
        }

        void ack4offset(Difference_len difference_, Packet_num_len pn_, bool value_){
            auto index = difference_ % get_capacity();
            data_[index].ack4offset(pn_, value_);
        }

        void ack4offset2(Difference_len difference_, size_t mapindex_, Packet_num_len pn_, bool maptype_, bool value_){
            auto index = difference_ % get_capacity();
            data_[index].ack4offset2(pn_, mapindex_, maptype_, value_);
        }

        void add_transmission(Difference_len difference_, Packet_num_len pn_, Offset_len off_){
            auto index = difference_ % get_capacity();
            data_[index].add_transmission(pn_, off_);
        }

        void add_retransmission(Difference_len difference_, Packet_num_len pn_, Offset_len off_){
            auto index = difference_ % get_capacity();
            data_[index].add_retransmission(pn_, off_);
        }

        size_t get_status(Difference_len difference_){
            auto index = difference_ % get_capacity();           
            return data_[index].metabuf.get_status();
        }

        bool emit(size_t index_, struct iovec& src_, ssize_t &len_, Offset_len &off_){
            return data_[index_].metabuf.emit(src_, len_, off_);
        }

        bool iscomplete(Difference_len difference_){
            auto index_ = difference_ % get_capacity();
            if (index_ >= get_capacity()){
                std::cerr << "bool iscomplete(Difference_len difference_) index out of boundary" << std::endl;
                _Exit(0);
            }
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
            if (i >= count_)
                throw std::out_of_range("Index out of range");
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

/*Record copy contiouns*/
class RecordInfo
{
    private:
        /* data */
        Difference_len record_diffference = 0;

        uint32_t record_acumulate = 0;

        size_t end_index = std::numeric_limits<size_t>::max();

        size_t start_index = std::numeric_limits<size_t>::max();

        Offset_len record_offset = 0;
    public:
        RecordInfo(/* args */){};
        ~RecordInfo(){};
        void set(uint16_t index_, Packet_len len_, Offset_len offset_, Difference_len difference_){
            start_index = end_index = index_;
            record_acumulate = len_;
            record_offset = offset_;
            record_diffference = difference_;
        } 

        void reset(){
            start_index = end_index = std::numeric_limits<size_t>::max();
            record_acumulate = 0;
            record_diffference = 0;
            record_offset = 0;
        }

        bool empty(){
            return (end_index == std::numeric_limits<size_t>::max()) && (start_index == std::numeric_limits<size_t>::max());
        }

        void update(uint32_t index_, size_t offset_, size_t len_, Difference_len difference_){
            /*Check difference*/
            if (get_start_index() == std::numeric_limits<size_t>::max()){
                set(index_, len_, offset_, difference_);
            }else{
                if (difference_ != record_diffference){
                    std::cerr << "RecordInfo: record_diffference(" << (int)record_diffference << " ), difference_(" << (int)difference_ <<")" <<std::endl;
                    _Exit(0);
                }
                end_index = index_;
                record_acumulate += len_;
                /*Add check for record len_*/
            }
        }

        size_t get_target_offset(){
            return (record_offset + record_acumulate);
        }

        size_t get_offset(){
            return record_offset;
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

        void print_log(){
            std::cout<<"[RecordInfo]:\n record_diffference: " <<(int)record_diffference 
                <<", record_acumulate:" <<record_acumulate
                <<", end_index:" << end_index
                <<", start_index:" << start_index
                <<", record_offset:" << record_offset << std::endl;
        }

        Difference_len get_record_difference(){
            return record_diffference;
        }
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

        uint16_t index_ = LIMIT_UINT16_T;

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
            index_ = LIMIT_UINT16_T;
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
            if (status_ == 3){
                completecheck();
            }
            return status_;
        }

        void set_difference(Difference_len difference_){
            rdifference = difference_;
        }

        bool set_start(size_t pos){
            if (index_ != LIMIT_UINT16_T){
                return false;
            }
            index_ = pos;
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

        bool find(Offset_len pkt_offset, Packet_len pkt_length){
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
            }
            return exist;
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
            if (received == total || complete_flag){
                status_ = 5;
            }
            status_ = 4;
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

        uint16_t get_position(){
            return index_;
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
        std::cout<<"push_back:" << tail_ << ", " << difference_ << std::endl;

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

    void insert(Difference_len difference, Offset_len pkt_offset, Packet_len pkt_length, bool &exist_) {
        auto index = difference % capacity_;
        inrangecheck(index, __func__);
        exist_ = data_[index].find(pkt_offset, pkt_length);
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
        data_[index].addexplen(expected);
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


    bool insertzero(Difference_len difference_, uint16_t position_){
        auto index_ = difference_ % get_capacity();
        inrangecheck(index_, __func__);
        return data_[index_].set_start(position_);
    }


    /* 
    Check whether the given index is within the current valid data range.
    If it is not within the range, advance tail_ by repeatedly calling push_back()
    until the index becomes part of the valid data.
    */ 
    void indexcheck(Difference_len difference_) {
        /*
        If the queue is empty, push_back() must be called once to add an element first.
        This ensures the valid range is no longer empty, allowing the index to be included in it.
        */
        auto index = difference_ % get_capacity();
        // std::cout << "index:" << index << ", " << difference_ << ", " << count_ << ", " << head_ << ", " << tail_ << std::endl;

        // if (empty()) {
        //     size_t desiredTail = (index + 1) % capacity_;
        //     /*
        //     Calculate the number of times push_back() needs to be called 
        //     (i.e., the number of steps to advance from the current tail_ to desiredTail)
        //     */ 
        //     size_t pushes = (desiredTail + capacity_ - tail_) % capacity_;
        //     for (size_t i = 0; i < pushes; ++i) {
        //         push_back(difference_);
        //     }
        //     return;
        // }
        
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
        inrangecheck(index, __func__);
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
        std::cout<<"receive condition"<<std::endl;
        for (auto i = 0; i < count_; i++){
            std::cout << "" << at(i).get_difference() << ", " << at(i).receive_offset.count() << std::endl;
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

    bool recv_flag;

    /*Acknowledge packet number*/
    uint64_t send_num;

    bool bidirect;

    Recovery recovery;

    PktNumSpace pkt_num_spaces;

    std::chrono::nanoseconds rtt;

    std::chrono::nanoseconds srtt;

    std::chrono::nanoseconds minrtt;

    std::chrono::nanoseconds rto;

    std::chrono::nanoseconds rttvar;
    
    std::chrono::steady_clock::time_point handshake;

    // RecvBuf rec_buffer;

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

    const uint8_t handshake_header[sizeof(Header)] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    const uint8_t fin_header[sizeof(Header)] = {7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

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

    size_t current_loop_min;

    size_t current_loop_max;

    size_t send_status_flag;

    /*
    ts_record is used to calculate approximate send ts for each packet.
    Approximate ts ~= (2nd ts - 1st ts)/(2nd pkt - 1st pkt + 1)
    */ 
    std::chrono::steady_clock::time_point start_ts;

    std::chrono::steady_clock::time_point end_ts;
    
    // Send message

    /* Replace the conbination of send_msg, send_iov and send_header to reduce packet genaratio cost*/
    std::vector<Message> send_message;

    std::vector<RCMessage> receive_message;
    
    // EAGAIN recovery.
    ssize_t start_index = -1;

    ssize_t end_index = -1;

    uint64_t ACKrange;

    ssize_t min_received = -1;

    // ssize_t max_received = -1;
    size_t max_received = std::numeric_limits<size_t>::max();

    bool difference_flag;

    size_t receive_upper_bound = 0;

    size_t receive_upper_limit = 0;

    size_t receive_upper_check = 0;

    /*Receive buffer*/
    std::vector<uint8_t> rx_buffer;

    std::vector<uint8_t> receive_available_map;

    TSCircularQueue tsInfo;

    SCircularQueue sendbufferqueue;

    RCircularQueue recvCQ;

    RecordInfo receive_record;

    /*Avoid multiple cwnd reduction in same tramsmission round*/
    bool first_loss;

    Packet_num_len max_acknowleged = LIMIT_UINT64_T;

    bool rtt_initial = true;

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
    recv_flag(false),
    send_num(0),
    rtt(0),
    srtt(0),
    minrtt(0),
    rto(0),
    rttvar(0),
    handshake(std::chrono::steady_clock::now()),
    bidirect(true),
    initial(false),
    dmludp_error(0),
    dmludp_error_sent(0),
    send_connection_difference(0),
    receive_connection_difference(0),
    receive_connection_difference_registration(LIMIT_UINT32_T),
    current_loop_min(0),
    current_loop_max(0),
    recovery(MAX_SEND_UDP_PAYLOAD_SIZE),
    difference_flag(false),
    send_status_flag(0),
    acknowldge_iov(3, {nullptr, 0}),
    receivevector(MAX_ACK_UDP_PAYLOAD_SIZE, 0),
    // receive_offset(MAX_SEND_UDP_PAYLOAD_SIZE),
    rx_buffer(MAX_SEND_UDP_PAYLOAD_SIZE * RX_CONST, 0),
    receive_available_map(RX_CONST, 0),
    first_loss(false)
    {
        memset(&acknowldge_msghdr, 0, sizeof(acknowldge_msghdr));
        acknowldge_msghdr.msg_iov = nullptr;
        acknowldge_msghdr.msg_iovlen = 0;

        send_message.resize(ONCE_SEND_LIMIT);

        receive_message.resize(RX_CONST);

        acknowldge_header.resize(sizeof(Header));
        set_receive_message();
    };

    ~Connection(){};

    void set_receive_message(){
        for (auto i = 0 ; i < receive_message.size(); ++i){
            receive_message[i].set_receive_message(rx_buffer.data() + MAX_SEND_UDP_PAYLOAD_SIZE * i, MAX_SEND_UDP_PAYLOAD_SIZE);
        }
    }

    void loss_reset(){
        first_loss = false;
    }


    void initial_rtt() {
        auto arrive_time = std::chrono::steady_clock::now();
        srtt = arrive_time - handshake;
        rttvar = srtt / 2;
        rto = srtt + 4 * rttvar;
    }

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
    void update_rtt(std::chrono::steady_clock::time_point send_time, std::chrono::steady_clock::time_point receive_time){
        if (rtt_initial){
            minrtt = rtt = srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - send_time);
            rttvar = srtt / 2;
            rto = srtt + 4 * rttvar;
            rtt_initial = false;
            // std::cout<<"RTO:"<<rto.count()<< ", srr:"<<srtt.count()<<", rtt:"<<std::chrono::duration_cast<std::chrono::nanoseconds>(rtt).count()<<std::endl;
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
            // std::cout<<"RTO:"<<rto.count()<<", "<<tmp_rttvar.count()<<", srr:"<<srtt.count()<<", rtt:"<<std::chrono::duration_cast<std::chrono::nanoseconds>(rtt).count()<<std::endl;
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
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
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
        for (auto i = 0 ; i <= receive_max_index; i++){
            if (receive_available_map[i] == 1)
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

            if (pkt_ty == Type::Stop){
                stop_flag = false;
                send_packet_type = Type::Stop;
                send_flag_ = false;
            }
        }
        recvCQ.receive_log();
        return send_flag_;
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
        Packet_num_len pkt_num = receive_message[index].get_packet_number();
        Offset_len pkt_offset = receive_message[index].get_packet_offset();
        Difference_len pkt_difference = receive_message[index].get_packet_difference();
        auto pkt_length = receive_message[index].get_packet_length();

        // std::cout<<(int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<std::endl;
        /* no operation for old packet*/
        if (pkt_num < current_loop_min){
            receive_available_map[index] = 0;
            return;
        }

        if (isfirst){
            std::cout<<(int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<std::endl;
        }
        
        /*Mark packet as to be processed*/
        receive_available_map[index] = 1;
        if (pkt_difference >= receive_connection_difference){
            if (pkt_offset == 0){
                std::cout<< "1 " << (int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<", "<<receive_connection_difference<<std::endl;
                if (recvCQ.differencecheck(pkt_difference)){
                    std::cout<<(int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<", "<<receive_connection_difference<<std::endl;
                    recvCQ.indexcheck(pkt_difference);
                    if(!recvCQ.insertzero(pkt_difference, index)){
                        receive_available_map[index] = 0;
                    }
                }else{
                    receive_available_map[index] = 0;
                }
            }
        }else{
            receive_available_map[index] = 0;
        }

        /*TODO(3.3): Rethink min_received is worth to keep*/
        if (min_received == -1){
            min_received = pkt_num;
        }else{
            if (pkt_num < min_received){
                min_received = pkt_num;
            }
        }
        // std::cout<<"2 pkt_num:"<<pkt_num<<", pkt_offset:"<<pkt_offset<<std::endl;

        if (max_received == std::numeric_limits<size_t>::max() || pkt_num > max_received){
            max_received = pkt_num;
            /* bit map substitude byte map*/
            send_num = pkt_num;
        }  

        
        size_t pos = pkt_num - current_loop_min;
        /*
        If the difference between packet number and current_loop_min is more than receivevector capacity, previous will not do any acknowledge
        */
        // if(pos > receivevector.size() * sizeof(uint8_t)){
        if(pos > 8000){
            current_loop_min = pkt_num;
            pos = 0;
        }
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        /*
        If byte_index is beyong the capacity of the receivevector.size(), splite ack to multiple acks 
        or drop start part
        */
        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range. (byte_index:"<< byte_index <<", "<< receivevector.size() 
            <<", "<<max_received<<", "<< current_loop_min <<")" << std::endl;
            _Exit(0);
        }

        if (pkt_difference >= receive_connection_difference){
            receivevector[byte_index] |= (1 << bit_index);  
            bool exist = false;
            recvCQ.insert(pkt_difference, pkt_offset, pkt_length, exist);
            if (exist){
                receive_available_map[index] = 0;
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
        hdr->difference = receive_connection_difference;

        size_t info_len = (max_received - current_loop_min + 1 + 7) / 8;
        hdr->pkt_length = info_len + sizeof(Packet_num_len);

        acknowldge_iov[0].iov_base = acknowldge_header.data();
        acknowldge_iov[0].iov_len = sizeof(Header);

        ACKrange = current_loop_min;
        acknowldge_iov[1].iov_base = &ACKrange;
        acknowldge_iov[1].iov_len = sizeof(Packet_num_len);

        acknowldge_iov[2].iov_base = receivevector.data();
        acknowldge_iov[2].iov_len = info_len;

        acknowldge_msghdr.msg_iov = &acknowldge_iov[0];
        acknowldge_msghdr.msg_iovlen = 3;

        // std::cout<<"send_acknowledge:"<<send_num<<", "<<ACKrange<<", "<<max_received<<std::endl;
        // log_print(receivevector.data(), info_len);


        send_packet_type = ty;
        return sizeof(Header) + hdr->pkt_length;
    }
    

    bool check_status(){
        // std::cout << "check_status cwnd left:" << recovery.cwnd_available() <<", "<<recovery.cwnd_enough()<<", "<<sendbufferqueue.ready()<<std::endl;
        if (recovery.cwnd_enough() && sendbufferqueue.ready()) return true;
        return false;
    }

    /*Update received difference record*/
    void update_receive_parameter(){
        current_loop_min = current_loop_max + 1;
    }


    /*Process acknowledge packet*/
    // void process_acknowledge(const size_t index_){
    //     auto pkt_num = receive_message[index_].get_packet_number();
    //     auto pkt_len = receive_message[index_].get_packet_length();
    //     auto pkt_difference = receive_message[index_].get_packet_difference();
    //     receive_available_map[index_] = 0;

    //     auto receivets = std::chrono::steady_clock::now();
    //     auto first_pn = *reinterpret_cast<const uint64_t*>(receive_message[index_].iov[1].iov_base);

    //     // std::cout<<"process_acknowledge:"<<pkt_difference<<", first_pn:"<<first_pn << ", " << pkt_num << ", " << (max_acknowleged+1)<<std::endl;


    //     if (first_pn >= (max_acknowleged + 1)){
    //         // std::cout<<"pkt_num:"<<pkt_num << ", " << first_pn << ", " << (max_acknowleged+1) << std::endl;
    //         auto ackts = tsInfo.removeBeforeValue(first_pn);
    //         if (ackts.has_value()){
    //             update_rtt(*ackts, receivets);
    //         }
    //     }
    //     sendbufferqueue.completecheck(pkt_difference);

        

    //     auto end_pn = pkt_num;
    //     bool loss = false;
    //     size_t total_send = end_pn - first_pn + 1;
    //     auto ack_src = reinterpret_cast<const uint8_t*>(receive_message[index_].iov[1].iov_base) + sizeof(uint64_t);
    //     size_t byte_index = 0;
    //     size_t bit_index = 0;

        
    //     /*TODO: process max_ack and first_pn*/
    //     auto sendbufferqueue_start_index = sendbufferqueue.start();
    //     auto pn = first_pn;
    //     // std::cout<<"first_pn:"<<first_pn<<", end_pn:"<<end_pn<<", "<<max_acknowleged<<std::endl;

    //     /*Check acknowledge packet loss*/
    //     /*
    //     1. first_pn < (max_acknowleged + 1) && end_pn < (max_acknowleged + 1)
    //         Just check unused
    //     2. first_pn < (max_acknowleged + 1) && end_pn >= (max_acknowleged + 1)
    //     3. first_pn > (max_acknowleged + 1)
    //         just
    //     4. first_pn == (max_acknowleged + 1)
    //         Just check used 

    //     Consider if pn is the old block.
    //     */
    //     if (first_pn != (max_acknowleged + 1)){
    //         /*timeout and before send message get ack message*/
    //         if(first_pn < (max_acknowleged + 1)){
    //             /*first_pn <= (max_acknowleged + 1) <= end_pn*/
    //             if (end_pn < (max_acknowleged + 1)){
    //                 while (true){
    //                     if (pn > end_pn || pn == (max_acknowleged + 1)){
    //                         break;
    //                     }

    //                     size_t i = 0;
    //                     std::tuple<Packet_num_len, Packet_num_len, size_t, bool> sendtuple = {LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false};
    //                     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                         i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                         sendtuple = sendbufferqueue.get_unused_range(i, pn);
                            
    //                         if (pn <= std::get<1>(sendtuple) && pn >= std::get<0>(sendtuple)){
    //                             break;
    //                         }
    //                     }
    //                     if (sendtuple == std::make_tuple(LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false)){
    //                         pn++;
    //                         continue;
    //                     }
    //                     std::cout<<"0 check:" << std::get<0>(sendtuple) << ", " << std::get<1>(sendtuple) << ", " << std::get<2>(sendtuple)<< ", "<< std::get<3>(sendtuple)<< std::endl;

    //                     while (pn >= std::get<0>(sendtuple) && pn <= std::get<1>(sendtuple)){
    //                         byte_index = (pn - first_pn) / 8;
    //                         bit_index = (pn - first_pn) % 8;
    //                         size_t value = (ack_src[byte_index] >> bit_index) & 1;
    //                         sendbufferqueue.ack4offset2(i, std::get<2>(sendtuple), pn, std::get<3>(sendtuple), (bool)value);
    //                         pn++;
    //                         if (pn > end_pn || pn == (max_acknowleged + 1)){
    //                             break;
    //                         }
    //                     }
    //                 }
    //                 // {
    //                 //     std::cout<<"process_acknowledge 1:" << end_pn << ", " << (max_acknowleged + 1) << std::endl;
    //                 //     auto sendbufferqueue_start_index = sendbufferqueue.start();
    //                 //     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                 //         int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                 //         auto difference_ = sendbufferqueue.data_[index].get_difference();
    //                 //         std::cout << difference_ << " " ;
    //                 //         sendbufferqueue.data_[index].metabuf.ack_check();
    //                 //     }
    //                 // }
    //             }else{
    //                 while (true){
    //                     if (pn > end_pn || pn == (max_acknowleged + 1)){
    //                         break;
    //                     }
    //                     size_t i = 0;
    //                     int result = 0;
    //                     std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
    //                     std::tuple<Packet_num_len, Packet_num_len, size_t, bool> sendtuple = {LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false};
    //                     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                         i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                         sendpair = sendbufferqueue.get_packet_range(i, pn);
    //                         if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
    //                             continue;
    //                         }
    //                         if (pn <= sendpair.second && pn >= sendpair.first){
    //                             result = 1;
    //                             break;
    //                         }
    //                     }
    //                     if (result == 0){
    //                         for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                             i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                             sendtuple = sendbufferqueue.get_cleared_packet_range(i, pn);
    //                             if (sendtuple == std::make_tuple(LIMIT_UINT64_T, LIMIT_UINT64_T, LIMIT_SIZE_T, false)){
    //                                 continue;
    //                             }
    //                             if (pn <= std::get<1>(sendtuple) && pn >= std::get<0>(sendtuple)){
    //                                 result = 2;
    //                                 break;
    //                             }
    //                         }
    //                     }
                        

    //                     // std::cout<<"4 check:" << std::get<0>(sendpair) << ", " << std::get<1>(sendpair) << ", " << pn << ", " << result << std::endl;
    //                     // std::cout<<"5 check:" << std::get<0>(sendtuple) << ", " << std::get<1>(sendtuple) << ", " << pn << std::endl;

    //                     if (result == 0){
    //                         pn++;
    //                     }else if(result == 1){
    //                         while (pn >= std::get<0>(sendpair) && pn <= std::get<1>(sendpair)){
    //                             byte_index = (pn - first_pn) / 8;
    //                             bit_index = (pn - first_pn) % 8;
    //                             size_t value = (ack_src[byte_index] >> bit_index) & 1;
    //                             sendbufferqueue.ack4offset(i, pn, (bool)value);
    //                             pn++;
    //                             if (pn > end_pn || pn == (max_acknowleged + 1)){
    //                                 break;
    //                             }
    //                         }
    //                     }else if(result == 2){
    //                         while (pn >= std::get<0>(sendtuple) && pn <= std::get<1>(sendtuple)){
    //                             byte_index = (pn - first_pn) / 8;
    //                             bit_index = (pn - first_pn) % 8;
    //                             size_t value = (ack_src[byte_index] >> bit_index) & 1;
    //                             sendbufferqueue.ack4offset2(i, std::get<2>(sendtuple), pn, std::get<3>(sendtuple), (bool)value);
    //                             pn++;
    //                             if (pn > end_pn || pn == (max_acknowleged + 1)){
    //                                 break;
    //                             }
    //                         }
    //                     }

    //                 }
    //                 // {
    //                 //     std::cout<<"process_acknowledge 2:" << end_pn << ", " << (max_acknowleged + 1) << std::endl;
    //                 //     auto sendbufferqueue_start_index = sendbufferqueue.start();
    //                 //     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                 //         int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                 //         auto difference_ = sendbufferqueue.data_[index].get_difference();
    //                 //         std::cout << difference_ << " " ;
    //                 //         sendbufferqueue.data_[index].metabuf.ack_check();
    //                 //     }
    //                 // }
    //             }
    //         }else{
    //             auto loss_pn = max_acknowleged + 1;
    //             while (true)
    //             {
    //                 if (loss_pn == first_pn){
    //                     break;
    //                 }
    //                 size_t i = 0;
    //                 Difference_len temp_dif = LIMIT_UINT32_T;
    //                 std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
    //                 for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //                     i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //                     temp_dif = sendbufferqueue.get_difference(i);
    //                     sendpair = sendbufferqueue.get_packet_range(i, loss_pn);
    //                     if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
    //                         continue;
    //                     }
    //                     if (loss_pn <= sendpair.second && loss_pn >= sendpair.first){
    //                         break;
    //                     }
    //                 }
    //                 if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
    //                     // std::cerr << "1 Acknowledge unknow packet(" << loss_pn << ")" << std::endl;
    //                     // _Exit(0);
    //                     loss_pn++;
    //                 }
    //                 auto compare_ = sendbufferqueue.compareIndices(i, pkt_difference);
    //                 // std::cout<<"2 check:"<<loss_pn<<", "<<compare_<<", "<<pkt_difference<<", "<<temp_dif<<std::endl;
    //                 // if (compare_ == 0){
    //                 if (compare_ != 1){    
    //                     std::cout<<"0 " << sendpair.first << ", " << sendpair.second << std::endl;
    //                     if (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
    //                         while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
    //                             sendbufferqueue.ack4offset(i, loss_pn, true);
    //                             loss_pn++;
    //                             if (loss_pn == first_pn){
    //                                 break;
    //                             }
    //                         }
    //                     }
    //                     else{
    //                         loss_pn++;    
    //                     }
    //                 }
    //                 else{
    //                     // std::cout<<"1 " << sendpair.first << ", " << sendpair.second << std::endl;
    //                     while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
    //                         sendbufferqueue.ack4offset(i, loss_pn, false);
    //                         loss_pn++;
    //                         if (loss_pn == first_pn){
    //                             break;
    //                         }
    //                     }
    //                 }
    //                 // std::cout<<"3 check"<<std::endl;
    //             }
    //             // {
    //             //     std::cout<<"process_acknowledge 3:" << loss_pn << ", " << first_pn << std::endl;
    //             //     auto sendbufferqueue_start_index = sendbufferqueue.start();
    //             //     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //             //         int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //             //         auto difference_ = sendbufferqueue.data_[index].get_difference();
    //             //         std::cout << difference_ << " " ;
    //             //         sendbufferqueue.data_[index].metabuf.ack_check();
    //             //     }
    //             // }
    //         }
            
    //     }
    //     // std::cout << "process_acknowledge:" << max_acknowleged << ", "<< pn << std::endl;
    //     while (true)
    //     {
    //         // std::cout<<"process_acknowledge 4:" << pn << ", " <<(max_acknowleged + 1) << std::endl;
    //         if (pn > end_pn){
    //             break;
    //         }
            
    //         size_t i = 0;
    //         std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
    //         for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //             i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //             // sendpair = sendbufferqueue.data_[i].get_packet_range(pn);
    //             sendpair = sendbufferqueue.get_packet_range(i, pn);
              
    //             if (pn <= sendpair.second && pn >= sendpair.first){
    //                 break;
    //             }
    //         }
    //         // std::cout<<(int)i<<", sendpair:" << sendpair.first << ", " << sendpair.second <<std::endl;
          
       
    //         if (sendpair.first > pn || pn > sendpair.second){
    //             pn++;
    //             continue;
    //         }
    //         while (pn >= sendpair.first && pn <= sendpair.second){
    //             // std::cout<<"process_acknowledge 3"<<std::endl;
    //             if (pn <= end_pn && pn >= first_pn){
    //                 byte_index = (pn - first_pn) / 8;
    //                 bit_index = (pn - first_pn) % 8;
    //                 size_t value = (ack_src[byte_index] >> bit_index) & 1;
    //                 if (value == 0){
    //                     // std::cout<<"pn:"<<pn<<" loss"<<std::endl;
    //                     loss = true;
    //                 }
    //                 // sendbufferqueue.data_[i].ack4offset(pn, (bool)value);
    //                 sendbufferqueue.ack4offset(i, pn, (bool)value);
    //                 pn++;
    //             }else{
    //                 break;
    //             }
    //         }
    //         // std::cout<<"process_acknowledge 4," << pn <<std::endl;
    //     }

    //     if (max_acknowleged == LIMIT_UINT64_T){
    //         max_acknowleged = end_pn;
    //     }else{
    //         if (end_pn > max_acknowleged){
    //             max_acknowleged = end_pn;
    //         }
    //     }
        
        
    //     // std::cout << "max_acknowleged: " << max_acknowleged << std::endl;
        
    //     if (loss && !first_loss){
    //         recovery.check_point();
    //         recovery.congestion_event(receivets);
    //         recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
    //         first_loss = true;
    //     }else{
    //         recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
    //     }

    //     {
    //         auto sendbufferqueue_start_index = sendbufferqueue.start();
    //         for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
    //             int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
    //             auto difference_ = sendbufferqueue.data_[index].get_difference();
    //             std::cout << difference_ << " " ;
    //             sendbufferqueue.data_[index].metabuf.ack_check();
    //         }
    //     }
    // }
    void process_acknowledge(const size_t index_){
        auto pkt_num = receive_message[index_].get_packet_number();
        auto pkt_len = receive_message[index_].get_packet_length();
        auto pkt_difference = receive_message[index_].get_packet_difference();
        receive_available_map[index_] = 0;

        auto receivets = std::chrono::steady_clock::now();
        auto first_pn = *reinterpret_cast<const uint64_t*>(receive_message[index_].iov[1].iov_base);

        // std::cout<<"process_acknowledge:"<<pkt_difference<<", first_pn:"<<first_pn << ", " << pkt_num << ", " << (max_acknowleged+1)<<std::endl;


        if (first_pn >= (max_acknowleged + 1)){
            // std::cout<<"pkt_num:"<<pkt_num << ", " << first_pn << ", " << (max_acknowleged+1) << std::endl;
            auto ackts = tsInfo.removeBeforeValue(first_pn);
            if (ackts.has_value()){
                update_rtt(*ackts, receivets);
            }
        }
        sendbufferqueue.completecheck(pkt_difference);

        auto end_pn = pkt_num;
        bool loss = false;
        size_t total_send = end_pn - first_pn + 1;
        auto ack_src = reinterpret_cast<const uint8_t*>(receive_message[index_].iov[1].iov_base) + sizeof(uint64_t);
        size_t byte_index = 0;
        size_t bit_index = 0;

        
        /*TODO: process max_ack and first_pn*/
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        auto pn = first_pn;
        // std::cout<<"first_pn:"<<first_pn<<", end_pn:"<<end_pn<<", "<<max_acknowleged<<std::endl;

        /*Check acknowledge packet loss*/
        /*
        1. first_pn < (max_acknowleged + 1) && end_pn < (max_acknowleged + 1)
            Just check unused
        2. first_pn < (max_acknowleged + 1) && end_pn >= (max_acknowleged + 1)
        3. first_pn > (max_acknowleged + 1)
            just
        4. first_pn == (max_acknowleged + 1)
            Just check used 

        Consider if pn is the old block.
        */
        if (first_pn != (max_acknowleged + 1)){
            /*timeout and before send message get ack message*/
            if(first_pn < (max_acknowleged + 1)){
                /*first_pn <= (max_acknowleged + 1) <= end_pn*/
                if (end_pn < (max_acknowleged + 1)){
                    pn = end_pn + 1;
                }else{
                    pn = (max_acknowleged + 1);
                }
            }else{
                auto loss_pn = max_acknowleged + 1;
                while (true)
                {
                    if (loss_pn == first_pn){
                        break;
                    }
                    size_t i = 0;
                    Difference_len temp_dif = LIMIT_UINT32_T;
                    std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
                    for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                        i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                        temp_dif = sendbufferqueue.get_difference(i);
                        sendpair = sendbufferqueue.get_packet_range(i, loss_pn);
                        if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
                            continue;
                        }
                        if (loss_pn <= sendpair.second && loss_pn >= sendpair.first){
                            break;
                        }
                    }
                    if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
                        // std::cerr << "1 Acknowledge unknow packet(" << loss_pn << ")" << std::endl;
                        // _Exit(0);
                        loss_pn++;
                    }
                    auto compare_ = sendbufferqueue.compareIndices(i, pkt_difference);
                    // std::cout<<"2 check:"<<loss_pn<<", "<<compare_<<", "<<pkt_difference<<", "<<temp_dif<<std::endl;
                    // if (compare_ == 0){
                    if (compare_ != 1){    
                        std::cout<<"0 " << sendpair.first << ", " << sendpair.second << std::endl;
                        if (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
                            while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
                                sendbufferqueue.ack4offset(i, loss_pn, true);
                                loss_pn++;
                                if (loss_pn == first_pn){
                                    break;
                                }
                            }
                        }
                        else{
                            loss_pn++;    
                        }
                    }
                    else{
                        // std::cout<<"1 " << sendpair.first << ", " << sendpair.second << std::endl;
                        while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
                            sendbufferqueue.ack4offset(i, loss_pn, false);
                            loss_pn++;
                            if (loss_pn == first_pn){
                                break;
                            }
                        }
                    }
                    // std::cout<<"3 check"<<std::endl;
                }
                // {
                //     std::cout<<"process_acknowledge 3:" << loss_pn << ", " << first_pn << std::endl;
                //     auto sendbufferqueue_start_index = sendbufferqueue.start();
                //     for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                //         int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                //         auto difference_ = sendbufferqueue.data_[index].get_difference();
                //         std::cout << difference_ << " " ;
                //         sendbufferqueue.data_[index].metabuf.ack_check();
                //     }
                // }
            }
            
        }
        // std::cout << "process_acknowledge:" << max_acknowleged << ", "<< pn << std::endl;
        while (true)
        {
            // std::cout<<"process_acknowledge 4:" << pn << ", " <<(max_acknowleged + 1) << std::endl;
            if (pn > end_pn){
                break;
            }
            
            size_t i = 0;
            std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
            for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                // sendpair = sendbufferqueue.data_[i].get_packet_range(pn);
                sendpair = sendbufferqueue.get_packet_range(i, pn);
              
                if (pn <= sendpair.second && pn >= sendpair.first){
                    break;
                }
            }
            // std::cout<<(int)i<<", sendpair:" << sendpair.first << ", " << sendpair.second <<std::endl;
          
       
            if (sendpair.first > pn || pn > sendpair.second){
                pn++;
                continue;
            }
            while (pn >= sendpair.first && pn <= sendpair.second){
                // std::cout<<"process_acknowledge 3"<<std::endl;
                if (pn <= end_pn && pn >= first_pn){
                    byte_index = (pn - first_pn) / 8;
                    bit_index = (pn - first_pn) % 8;
                    size_t value = (ack_src[byte_index] >> bit_index) & 1;
                    if (value == 0){
                        // std::cout<<"pn:"<<pn<<" loss"<<std::endl;
                        loss = true;
                    }
                    // sendbufferqueue.data_[i].ack4offset(pn, (bool)value);
                    sendbufferqueue.ack4offset(i, pn, (bool)value);
                    pn++;
                }else{
                    break;
                }
            }
            // std::cout<<"process_acknowledge 4," << pn <<std::endl;
        }

        if (max_acknowleged == LIMIT_UINT64_T){
            max_acknowleged = end_pn;
        }else{
            if (end_pn > max_acknowleged){
                max_acknowleged = end_pn;
            }
        }
        
        
        // std::cout << "max_acknowleged: " << max_acknowleged << std::endl;
        
        if (loss && !first_loss){
            recovery.check_point();
            recovery.congestion_event(receivets);
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
            first_loss = true;
        }else{
            recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
        }

        {
            auto sendbufferqueue_start_index = sendbufferqueue.start();
            for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                int index = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                auto difference_ = sendbufferqueue.data_[index].get_difference();
                std::cout << difference_ << " " ;
                sendbufferqueue.data_[index].metabuf.ack_check();
            }
        }
    }


    void clear_recv_setting(){
        // receive_offset.clear();
    }

    // void recv_reset(){
    //     rec_buffer.reset();
    // }

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

     bool zerocheck(){
        if (!recvCQ.empty()){
            if(receive_connection_difference == recvCQ.start()){
                return true;
            }
        }
       
        return false;
    }

    void rx_len(size_t expected){
        recvCQ.rx_len(receive_connection_difference, expected);
    }

    void rx_set(size_t expected, uint8_t * target_){
        recvCQ.rx_len(receive_connection_difference, expected);
        recvCQ.set_recv_pointer(receive_connection_difference, target_);
    }

    void set_send_time(){
        handshake = std::chrono::steady_clock::now();
    }
    

    bool get_data(struct iovec* iovecs, int iovecs_len, int type_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
        /*
        triger data preparation
        */
        bool completed = true;
	    dmludp_error_sent = 0;

        if (sendbufferqueue.full()){
            return false;
        }

        sendbufferqueue.push_back(iovecs, iovecs_len, type_, priotity_list);
        
        // recovery.bytes_in_flight = 0;
        set_handshake();
        return completed;
    }

    // size_t get_once_data_len(){
    //     return written_data_once;
    // }

    // void clear_sent_once(){
    //     written_data_once = 0;
    // }

    /*If timer triggered, process all unacknowledge packet as loss*/
    /*TODD: use previous record pair to minimize the iteration times*/
    void process_timeout(){
        char ipStr[INET6_ADDRSTRLEN];
        uint16_t port = 0;

        if (peeraddr.ss_family == AF_INET) {
            // IPv4
            const sockaddr_in* addr_in = reinterpret_cast<const sockaddr_in*>(&peeraddr);
            inet_ntop(AF_INET, &(addr_in->sin_addr), ipStr, sizeof(ipStr));
            port = ntohs(addr_in->sin_port);
        } else if (peeraddr.ss_family == AF_INET6) {
            // IPv6
            const sockaddr_in6* addr_in6 = reinterpret_cast<const sockaddr_in6*>(&peeraddr);
            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), ipStr, sizeof(ipStr));
            port = ntohs(addr_in6->sin6_port);
        } else {
            std::cerr << "Unknown address family: " << peeraddr.ss_family << std::endl;
            return;
        }

        std::cout << "IP: " << ipStr << ", Port: " << port << std::endl;
        auto pn = max_acknowleged + 1;
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        auto max_sent_pn = pkt_num_spaces.getpktnum();
        std::cout<<"pn:"<<pn<<", "<<max_sent_pn<<std::endl;
        while (true)
        {
            if (pn > max_sent_pn){
                break;
            }
            size_t i = 0;
            std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
            for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                sendpair = sendbufferqueue.get_packet_range(i, pn);
                
                if (pn <= sendpair.second && pn >= sendpair.first){
                    break;
                }
            }
            // if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
            //     pn++;
            //     continue;
            // }
            if (pn < sendpair.first || pn > sendpair.second){
                pn++;
                continue;
            }
            // if (i == 0 && (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T))){
            //     std::cerr << "Acknowledge unknow packet(" << pn << ")" << std::endl;
            //     _Exit(0);
            // }
            // std::cout<<sendpair.first<<", " << sendpair.second << std::endl;
            while (pn >= sendpair.first && pn <= sendpair.second){
                sendbufferqueue.ack4offset(i, pn, false);
                pn++;
            }
        }
        auto total_send = max_sent_pn - max_acknowleged;

        max_acknowleged = max_sent_pn;
        auto ackts = tsInfo.removeBeforeValue(max_acknowleged);
        // std::cout<<"tsInfo.size:"<<tsInfo.size()<<", "<<max_acknowleged<<std::endl;

        auto receivets = std::chrono::steady_clock::now();

        recovery.on_packet_ack(total_send, receivets, std::chrono::duration_cast<std::chrono::seconds>(minrtt));
    }

    ssize_t prepareData() {
        Type ty = Type::Application;
        ssize_t out_len = 0; 
        Offset_len out_off = 0;
        size_t i = 0;
        ssize_t tramssioning_index = -1;

        size_t cwnd_limit = recovery.cwnd_available();
        if (cwnd_limit <= 0){
            return 0;
        }

        const size_t sent_limit = cwnd_limit;
        size_t sent = 0;      
        size_t sent_cwnd = 0; 
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++) {
            i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
            int d_sent = 0;
            auto pkg_difference = sendbufferqueue.get_difference(i);
            // std::cout<<"prepareData:"<<pkg_difference<<std::endl;
            while (true){
                size_t send_status = sendbufferqueue.get_status(i);
                if (i < 0 || i > sendbufferqueue.get_capacity() || sent > send_message.size()){
                    std::cout<<"i:"<<i<<", sent:"<<sent<<std::endl;
                    _Exit(0);
                }
                auto s_flag = sendbufferqueue.emit(i, send_message[sent].iov[1], out_len, out_off);
                // auto s_flag = sendbufferqueue.data_[i].metabuf.emit(send_message[sent].iov[1], out_len, out_off);
                /*auto s_flag = sendbufferqueue.emit(i, send_message[sent].iov[1], out_len, out_off);*/
                
                if (out_len == -1) {
                    break;
                }

                auto pn = pkt_num_spaces.updatepktnum();
                if (out_off == 0 && out_len > -1){
                    std::cout<<"[Debug] difference:"<< pkg_difference <<", pn:"<< pn <<", out_len:"<<out_len<<", out_off:"<<out_off<<std::endl;
                }
                send_message[sent].setMessageHeader(pn, out_off, pkg_difference, (Packet_num_len)out_len);
                recovery.on_packet_sent(out_len);
                
                if (send_status == 1){
                    sendbufferqueue.add_transmission(i, pn, out_off);
                }else if(send_status == 2){
                    sendbufferqueue.add_retransmission(i, pn, out_off);
                }
                sent++;
                sent_cwnd += out_len;
                d_sent++;
                if (sent_cwnd >= sent_limit || sent >= send_message.size()){
                    // std::cout<<"last pn:"<<pn<<", send_status:" <<send_status<<std::endl;
                    /*TODO add pakcet number-offset mapping*/
                    break;
                }           
            }
            // std::cout<<"prepareData:"<<(int)i<<", "<<d_sent<<", "<<sent_limit<<std::endl;
            if (sent_cwnd >= sent_limit || sent >= send_message.size()){
                break;
            }
        }
        // std::cout << "cwnd left:" << recovery.cwnd_available() <<std::endl;
        // std::cout << "sent:" << sent << std::endl;

        return sent;
    }
    
    /*Check send or received data*/
    void log_print(void* src_, size_t len_) {
        if (!src_) {
            std::cerr << "Null pointer passed to log_print!" << std::endl;
            return;
        }
        
        auto* data = static_cast<uint8_t*>(src_);  

        for (size_t i = 0; i < len_; i++) {
            std::cout << static_cast<int>(data[i]) << " ";  
        }
        std::cout << std::endl;
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
    void send_packet_complete(size_t err_ = 0, size_t sent = 0, const std::chrono::steady_clock::time_point& start_ts = std::chrono::steady_clock::time_point{}){
        if(send_packet_type == 0){
            return;
        }
        set_error2(err_);
        if (err_ != 0){
            if (send_packet_type == Type::Application){
                end_ts = std::chrono::steady_clock::now();
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
            for (size_t i = 0; i < receivevector.size(); ++i) {
                receivevector[i] = 0;
            }
            min_received = -1;
            current_loop_min = max_received + 1;
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
                if (receive_available_map[idx] == 0){
                    break;
                }
                idx++;
                if (idx == receive_available_map.size()){
                    return std::numeric_limits<size_t>::max();
                }
            }
            return idx;
        }
        auto idx = index + 1;
        while(true){
            if (receive_available_map[idx] == 0){
                break;
            }
            idx++;
            if (idx == receive_available_map.size()){
                return std::numeric_limits<size_t>::max();
            }
        }
        return idx;
    }


    bool registration_check(){
        return receive_connection_difference == receive_connection_difference_registration;
    }

    // 3.27 recvCQ.data_ cannot be directly access
    void process_application_copy(){
        Offset_len pkt_offset;
        Packet_len pkt_len;
        Difference_len pkt_difference;
        if (!recvCQ.empty()){
            std::cout<<"receive_connection_difference:" << receive_connection_difference << ", " << recvCQ.start() << ", " << recvCQ.srcsetcheck(receive_connection_difference) << ", " << recvCQ.get_status(receive_connection_difference) << std::endl;
            if (receive_connection_difference == recvCQ.start() && (recvCQ.get_status(receive_connection_difference) == 2) && recvCQ.srcsetcheck(receive_connection_difference)){
                auto index = recvCQ.startpos();
                pkt_offset = receive_message[index].get_packet_offset();
                pkt_difference = receive_message[index].get_packet_difference();
                // std::cout<<"receive_connection_difference:" << receive_connection_difference << ", " << pkt_difference << ", " << recvCQ.start() << ", " << pkt_offset << std::endl;
                recvCQ.copy(pkt_difference, pkt_offset, receive_message[index].iov[1].iov_base, 48);
                receive_available_map[index] = 0;
                receive_record.reset();
                receive_connection_difference_registration = receive_connection_difference;
                return;
            }
        }
        

        /*TODO: used count to reduce iteration times*/
        for (auto index = 0; index < receive_available_map.size(); index++){
            pkt_difference = receive_message[index].get_packet_difference();
            if (receive_available_map[index] == 0){
                if (!receive_record.empty()){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_difference = receive_record.get_record_difference();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        // std::cout<<"3 copy"<<std::endl;
                        recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    receive_record.reset();
                }   
                continue;
            }

            /*No longer process old data block*/
            if (pkt_difference < receive_connection_difference){
                receive_available_map[index] = 0;
                if (!receive_record.empty()){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_difference = receive_record.get_record_difference();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        // std::cout<<"1 copy"<<std::endl;
                        recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    receive_record.reset();
                }   
                continue;
            }

            if (receive_connection_difference == pkt_difference && recvCQ.targetCheck(pkt_difference)){
                pkt_offset = receive_message[index].get_packet_offset();
                pkt_len = receive_message[index].get_packet_length();

                receive_available_map[index] = 0;
                if (recvCQ.copyed_check(pkt_difference, pkt_offset)){
                    if (!receive_record.empty()){
                        auto copy_len = receive_record.get_acumulation();
                        auto copy_index = receive_record.get_start_index();
                        auto copy_difference = receive_record.get_record_difference();
                        auto copy_offset = receive_record.get_offset();
                        if (copy_offset >= 48){
                            // std::cout<<"2 copy:"<<pkt_offset<<std::endl;
                            recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                        }else{
                            recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        }
                        receive_record.reset();
                    }   
                    continue;
                }

                if(receive_record.get_end_index() - receive_record.get_start_index() == 7){ 
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        // std::cout<<"4 copy"<<std::endl;
                        recvCQ.copy(pkt_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);                        
                    }else{
                        recvCQ.copy(pkt_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
                    recvCQ.record_copy(pkt_difference, pkt_offset);
                    if(recvCQ.processComplete(pkt_difference)){
                        receive_record.reset();
                        return;
                    }
                    continue;
                }

                if (receive_record.get_target_offset() != pkt_offset && receive_record.get_target_offset() != 0){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        // std::cout<<"5 copy"<<std::endl;
                        recvCQ.copy(pkt_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(pkt_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
                    recvCQ.record_copy(pkt_difference, pkt_offset);
                    if(recvCQ.processComplete(pkt_difference)){
                        receive_record.reset();
                        return;
                    }
                    continue;
                }
                // std::cout<<"update:"<<pkt_offset<<", "<<recvCQ.copyed_check(pkt_difference, pkt_offset)<<std::endl;
                recvCQ.record_copy(pkt_difference, pkt_offset);
                receive_record.update(index, pkt_offset, pkt_len, pkt_difference);
            }else{
                /*different block occurs, contious check stop and start copy*/
                receive_upper_check = index;
                if (!receive_record.empty()){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_difference = receive_record.get_record_difference();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        std::cout<<"6 copy"<<std::endl;
                        recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    receive_record.reset();
                    if (copy_difference != receive_connection_difference){
                        std::cerr << "[process_application_copy check 3]: copy_difference(" << (int)copy_difference<<"), receive_connection_difference(" << (int)receive_connection_difference<<")"<<std::endl;
                        _Exit(0);
                    }
                    if(recvCQ.processComplete(copy_difference)){
                        return;
                    }
                }  
            }
        }


        if (!receive_record.empty()){
            auto copy_len = receive_record.get_acumulation();
            auto copy_index = receive_record.get_start_index();
            auto copy_difference = receive_record.get_record_difference();
            auto copy_offset = receive_record.get_offset();
            if (copy_offset >= 48){
                std::cout<<"7 copy"<<std::endl;
                recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
            }else{
                recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
            }
            receive_record.reset();
        }   

        recvCQ.processCheck(receive_connection_difference);

        receive_record.reset();
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
    // size_t send_data(msghdr)
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

    // size_t read(uint8_t* out, bool iscopy, size_t output_len = 0){
    //     return rec_buffer.emit(out, iscopy, output_len);
    // };

    void reset(){
        // norm2_vec.clear();
    };

    void set_handshake(){
        handshake = std::chrono::steady_clock::now();
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