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


namespace dmludp {

const size_t HEADER_LENGTH = sizeof(Header);

// The default max_datagram_size used in congestion control.
const size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1440;

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

using Priority_len = uint8_t;

using Offset_len = uint32_t;

using Acknowledge_sequence_len = uint64_t;

using Difference_len = uint8_t;

using Acknowledge_time_len = uint8_t;

using Packet_len = uint16_t;

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
    using DataType = std::pair<std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>,
                               std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>>;
    using TimeStamp = std::chrono::high_resolution_clock::time_point;
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
            if (item.first.first <= value && item.second.first >= value) {
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
        DynamicBitset RCset_body;

        size_t payload_len = MAX_SEND_UDP_PAYLOAD_SIZE;

        std::vector<size_t> dataload_len;

        size_t dataload_index = 0;

        bool used_flag = false;

        RCset(size_t capacity_ = 330000): 
        RCset_body(capacity_){
            dataload_len.reserve(2);
        }

        bool find(uint64_t offset_){
            auto index = get_index(offset_);
            if (index >= RCset_body.size()){
                std::cerr << "index:" << index << ", RCset_body.size:" << RCset_body.size() << std::endl;
                throw std::underflow_error("[RCset]: find index beyond capacity_");
            }
            return RCset_body[index] == 1;
        }

        void insert(uint64_t offset_){
            auto index = get_index(offset_);
            if (index >= RCset_body.size()){
                throw std::underflow_error("[RCset]: insert index beyond capacity_");
            }
            RCset_body[index] = 1;
        }

        void add_rule(size_t load_len){
            dataload_len.push_back(load_len);
        }

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
            // if (RCset_body.size() > 6000 && used_flag == true){
            //     RCset_body.resize(6000);
            // }
            RCset_body.clear();
            dataload_len.clear();
            dataload_index = 0;
            used_flag = true;
        }

        ~RCset(){}
};

/*Used to record retransmission data*/
class ReTransmissionMap{
    private:
        size_t start_packet = std::numeric_limits<size_t>::max();

        size_t end_packet = std::numeric_limits<size_t>::max();

        std::vector<Offset_len> offsets;

    public:
        ReTransmissionMap():offsets(ReTransmissionMapLimit, 0){
            // offsets.resize(2000);
        };

        ~ReTransmissionMap(){};

        void clear(){
            start_packet = end_packet = std::numeric_limits<size_t>::max();
            std::fill(offsets.begin(), offsets.end(), 0);
        }

        Offset_len get_offset(Packet_num_len packetnum){
            if (packetnum < start_packet || packetnum > end_packet){
                return LIMIT_UINT32_T;
            }
            // if (packetnum < start_packet){
            //     std::cerr << "ReTransmissionMap get_offset() out of boundary(" << start_packet << ", " << packetnum << ")" << std::endl;
            //     _Exit(0);
            // }
            // if ((packetnum - start_packet) > (offsets.size() - 1)){
            //     std::cerr << "ReTransmissionMap get_offset() out of boundary(" << start_packet << ", " << packetnum << ")" << std::endl;
            //     _Exit(0);
            // } 
            return offsets[packetnum - start_packet];
        }

        void add(Packet_num_len packetnum, Offset_len packetoffset){
            if(start_packet == std::numeric_limits<size_t>::max()){
                start_packet = packetnum;
            }
            // if (start_packet == std::numeric_limits<size_t>::max()){
            //     throw std::underflow_error("Error: start_packet is -1");
            // }
            if (end_packet == std::numeric_limits<size_t>::max()){
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
        }

        bool empty(){
            if (start_packet == std::numeric_limits<size_t>::max() && end_packet != std::numeric_limits<size_t>::max()) {
                throw std::underflow_error("Error: start == MAX, end != MAX");
            }
            return ((start_packet == end_packet) && (start_packet == std::numeric_limits<size_t>::max()));
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
        std::pair<size_t, Offset_len> startmap;
        
        std::pair<size_t, Offset_len> endmap;

    public:
        TransmissionMap() : startmap(std::numeric_limits<size_t>::max(), 0), endmap(std::numeric_limits<size_t>::max(), 0){};

        ~TransmissionMap(){};

        void add(Packet_num_len packetnum, Offset_len packetoffset){
            if(startmap.first == std::numeric_limits<size_t>::max()){
                startmap = std::make_pair(packetnum, packetoffset);
            }
            // if(startmap.first == std::numeric_limits<size_t>::max()){
            //     throw std::underflow_error("Error: startmap.first is -1");
            // }
            if ((endmap.first + 1) == packetnum || endmap.first == std::numeric_limits<size_t>::max()){
                if((endmap.first + 1) == packetnum){
                    if(endmap.second + 1440 != packetoffset && endmap.second != 0){
                        std::cout<<"[Error] endmap.second:"<<endmap.second<<", "<<packetoffset<<std::endl;
                       _Exit(0);
                    }
                }
                endmap = std::make_pair(packetnum, packetoffset);
            }else{
                throw std::underflow_error("Error: TransmissionMap lacks enough space");
            }      
            // std::cout<<"startmap:"<<startmap.first<<", "<<startmap.second<<", endmap:"<<endmap.first<<", "<<endmap.second<<", "<<packetnum<<", packetoffset:"<<packetoffset<<std::endl;
        }

        bool empty() {
            if (startmap.first == std::numeric_limits<size_t>::max() && endmap.first != std::numeric_limits<size_t>::max()) {
                throw std::underflow_error("Error: start == MAX, end != MAX");
            }
            return ((startmap.first == endmap.first) && (endmap.first == std::numeric_limits<size_t>::max()));
        }  

        void clear(){
            startmap = endmap = std::make_pair(std::numeric_limits<size_t>::max(), 0);
        }

        std::pair<Packet_num_len, Packet_num_len> get_range(){
            return std::make_pair(startmap.first, endmap.first);
        }

        Offset_len get_offset(Packet_num_len packetnum_){
            Offset_len offset_ = LIMIT_UINT32_T;
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

/*TransmissionMap and Retransmission queue*/
// template<typename T, typename = typename std::enable_if<
//     std::is_same<T, TransmissionMap>::value || std::is_same<T, ReTransmissionMap>::value>::type>
// class MapSet {
//     private:
//         std::vector<T> buffer_;

//         size_t head_;

//         size_t tail_;

//         size_t capacity_;

//         size_t count_;
//     public:
//         explicit MapSet(size_t capacity = MapSetLimit)
//             : buffer_(capacity), capacity_(capacity),
//             head_(0), tail_(0), count_(0) {}

//         bool empty() const {
//             return count_ == 0;
//         }

//         bool full() const {
//             return count_ == capacity_;
//         }

//         size_t size() const {
//             return count_;
//         }

//         size_t capacity() const {
//             return capacity_;
//         }

//         size_t used() const{
//             return count_;
//         }

//         // void push(const T& value) {
//         void push() {
//             if (full()) {
//                 throw std::overflow_error("MapSet is full");
//             }
//             buffer_[tail_].clear();
//             tail_ = (tail_ + 1) % capacity_;
//             ++count_;
//         }

//         void pop() {
//             if (empty()) {
//                 return;
//             }
//             auto frontmap = front();
//             frontmap.clear();
//             head_ = (head_ + 1) % capacity_;
//             --count_;
//         }

//         // T& front() {
//         //     if (empty()) {
//         //         throw std::underflow_error("MapSet is empty");
//         //     }
//         //     return buffer_[head_];
//         // }

//         // const T& front() const {
//         //     if (empty()) {
//         //         throw std::underflow_error("MapSet is empty");
//         //     }
//         //     return buffer_[head_];
//         // }

//         // T& back() {
//         //     if (empty()) {
//         //         throw std::underflow_error("MapSet is empty");
//         //     }
//         //     return buffer_[(tail_ + capacity_ - 1) % capacity_];
//         // }

//         // const T& back() const {
//         //     if (empty()) {
//         //         throw std::underflow_error("MapSet is empty");
//         //     }
//         //     return buffer_[(tail_ + capacity_ - 1) % capacity_];
//         // }

//         T* front() {
//             if (empty()) {
//                 return nullptr;
//             }
//             return &buffer_[head_];
//         }

//         const T* front() const {
//             if (empty()) {
//                 return nullptr;
//             }
//             return &buffer_[head_];
//         }

//         T* back() {
//             if (empty()) {
//                 return nullptr;
//             }
//             return &buffer_[(tail_ + capacity_ - 1) % capacity_];
//         }

//         const T* back() const {
//             if (empty()) {
//                 return nullptr;
//             }
//             return &buffer_[(tail_ + capacity_ - 1) % capacity_];
//         }

//         void add(uint64_t packetnum_, Offset_len packetoffset_){
//             auto backmap = back();
//             if (backmap == nullptr){
//                 push();
//                 auto newmap = back();
//                 newmap.add(packetnum_, packetoffset_);
//                 return;
//             }

//             auto maprange = backmap.get_range();
//             if ((packetnum_ == maprange.second + 1) && !backmap.full())
//             {
//                 backmap.add(packetnum_, packetoffset_);
//             }else{
//                 if (full()){
//                     std::cerr << typeid(T).name() << " MapSet is full" << std::endl;
//                     _Exit(0);
//                 }
//                 push();
//                 auto newmap = back();
//                 newmap.add(packetnum_, packetoffset_);
//             }
//         }
        
//         /*Fetch the head map range info*/
//         std::pair<Packet_num_len, Packet_num_len> get_range(){
//             return buffer_[head_].get_range();
//         }
        
//         /*Remove all old data older the packet number*/
//         void removeBeforeValue(Packet_num_len packet_){
//             auto index = head_;
//             for (auto i = 0; i < used(); i++){
//                 auto range = get_range();
//                 if (range.second < packet_){
//                     pop();
//                 }else{
//                     return;
//                 }
//             }
//         }

//         void for_each(const std::function<void(const T&)>& func) const {
//             size_t idx = head_;
//             for (size_t i = 0; i < count_; ++i) {
//                 func(buffer_[idx]);
//                 idx = (idx + 1) % capacity_;
//             }
//         }

//         void clear(){
//             size_t idx = head_;
//             for (size_t i = 0; i < count_; ++i) {
//                 buffer_[idx].clear();
//                 idx = (idx + 1) % capacity_;
//             }
//             head_ = 0;
//             tail_ = 0;
//             count_ = 0;
//         }


//         Offset_len get_offset(Packet_num_len PacketNum){
//             size_t idx = head_;
//             Offset_len result_offset = 0;
//             for (size_t i = 0; i < count_; ++i) {
//                 result_offset = buffer_[idx].get_offset(PacketNum);
//                 if (result_offset != LIMIT_UINT32_T){
//                     if (i != 0)
//                     {
//                         removeBeforeValue(PacketNum);
//                     }
//                     return result_offset;
//                 }
//                 idx = (idx + 1) % capacity_;
//             }
//             return  LIMIT_UINT32_T;
//         }

// };
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
            buffer_[tail_].clear();
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

        void add(uint64_t packetnum_, Offset_len packetoffset_) {
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
            while (!empty()) {
                auto range = get_range();
                std::cout<<"range:"<<range.first<<", "<<range.second<<std::endl;
                if (range.second < packet_ && range.second != LIMIT_UINT64_T) {
                    pop();
                } else {
                    return;
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
            // buffer_.clear();
            // buffer_.resize(capacity_);
            for (auto e: buffer_){
                e.clear();
            }
            head_ = 0;
            tail_ = 0;
            count_ = 0;
        }

        Offset_len get_offset(Packet_num_len PacketNum) {
            size_t idx = head_;
            for (size_t i = 0; i < count_; ++i) {
                Offset_len result_offset = buffer_[idx].get_offset(PacketNum);
                if (result_offset != LIMIT_UINT32_T) {
                    if (i != 0) {
                        removeBeforeValue(PacketNum);
                    }
                    return result_offset;
                }
                idx = (idx + 1) % capacity_;
            }
            return LIMIT_UINT32_T;
        }
};

// New TransmissionMap
// class TransmissionMap{
//     private:
//         std::vector<std::pair<ssize_t, uint32_t>> startmap;

//         std::vector<std::pair<ssize_t, uint32_t>> endmap;

//         size_t count_ = 0; // Total elements in TransmissionMap

//         size_t head_ = 0;  // Start index

//         size_t tail_ = 0;  // Max available index

//         ssize_t offset_ = -1; // Used for in range

//         size_t capacity_; // TransmissionMap capacity
//     public:
//         TransmissionMap() : startmap(10), endmap(10), capacity_(10){};

//         ~TransmissionMap(){};

//         void add(uint64_t packetnum, uint32_t packetoffset){
//             if (empty()){
//                 push_back(packetnum, packetoffset);
//             }

//             for (auto i = start(); i < end();i++){
//                 if ((endmap[i].first + 1) == packetnum){
//                     endmap[i] = std::make_pair(packetnum, packetoffset);
//                     return;
//                 }else{
//                     push_back(packetnum, packetoffset);
//                 }
//             }
//         }

//         size_t start(){
//             return head_;
//         }

//         size_t end(){
//             return tail_;
//         }

//         void push_back(uint64_t packetnum, uint32_t packetoffset) {
//             startmap[tail_] = std::make_pair(packetnum, packetoffset);
//             endmap[tail_] = std::make_pair(packetnum, packetoffset);
//             tail_ = (tail_ + 1) % capacity_;
//             count_++;
//         }

//         void pop_front() {
//             if (empty()) {
//                 throw std::runtime_error("TransmissionMap is empty, cannot remove element.");
//             }
//             startmap[tail_] = std::make_pair(-1, 0);
//             endmap[tail_] = std::make_pair(-1, 0);
//             head_ = (head_ + 1) % capacity_;
//             count_--;
//         }


//         bool empty(){
//             return count_ == 0;
//         }

//         bool inrange(size_t PacketNum){
//             bool result = false;
//             size_t index_ = 0;
//             for (auto i = start(); i < end();i = (i + 1)%capacity_){
//                 if (PacketNum >= startmap[i].first && PacketNum <= endmap[i].first){
//                     index_ = i;
//                     if (startmap[i].first == 0){
//                         if (PacketNum == startmap[i].first){
//                             offset_ = startmap[i].second;
//                         }else{
//                             offset_ = 48 + (PacketNum - startmap[i].first - 1) * MAX_SEND_UDP_PAYLOAD_SIZE;
//                         }
//                     }else{
//                         offset_ = (PacketNum - startmap[i].first) * MAX_SEND_UDP_PAYLOAD_SIZE + startmap[i].first;
//                     }
//                     removeBeforeValue(index_);
//                     return true;
//                 }
//                 index_++;
//             }
//             return result;
//         } 

//         void removeBeforeValue(size_t index_){
//             head_ = (head + index_) % capacity;
//             count -= index_;
//         }

//         bool full() const {
//             return count_ == capacity_;
//         }

//         uint32_t get_offset(uint64_t packetnum_){
//             auto result = offset_;
//             offset_ = -1;
//             return result;
//         }


//         void clear(){
//             tail_ = head_ = count_ = 0;
//         }

// };

/*old*/
// class MetaInfo{
//     public:
//         SendBuf metabuf;

//         const uint8_t MetaDifference;

//         ReTransmissionMap retransmission_map;

//         std::vector<uint32_t> priority_offset;

//         TransmissionMap transmission_map;

//         uint16_t difference_flag = std::numeric_limits<uint16_t>::max();

//         size_t range_len;

//         size_t send_len;

//         size_t block_type = std::numeric_limits<size_t>::max();

//         std::pair<size_t, size_t> packet_range{std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};

//         MetaInfo(size_t difference_flag_ = std::numeric_limits<uint16_t>::max()): 
//         // difference_flag(difference_flag_), 
//         MetaDifference(difference_flag_),
//         metabuf(MAX_SEND_UDP_PAYLOAD_SIZE),
//         range_len(0),
//         send_len(0){};

//         ~MetaInfo(){};

//         void add_transmission(uint64_t packetnum_, uint32_t packetoffset_){
//             transmission_map.add(packetnum_, packetoffset_);
//             if (packet_range.first == std::numeric_limits<size_t>::max()){
//                 packet_range = std::make_pair(packetnum_, packetnum_);
//             }else{
//                 if (packet_range.second < packetnum_){
//                     packet_range.second = packetnum_;
//                 }
//             }
//         }

//         void add_retransmission(uint64_t packetnum_, uint32_t packetoffset_){
//             retransmission_map.add(packetnum_, packetoffset_);
//             if (packet_range.first == std::numeric_limits<size_t>::max()){
//                 packet_range = std::make_pair(packetnum_, packetnum_);
//             }else{
//                 if (packet_range.second < packetnum_){
//                     packet_range.second = packetnum_;
//                 }
//             }
//         }

//         void reset_packet_range(){
//             packet_range = std::make_pair(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
//         }

//         std::pair<uint64_t, uint64_t> get_packet_range(){
//             return packet_range;
//         }

//         bool intransmissionmap(uint64_t PacketNum){
//             return transmission_map.inrange(PacketNum);
//         }

//         bool inretransmissionmap(uint64_t PacketNum){
//             return retransmission_map.inrange(PacketNum);
//         }

//         ssize_t offset_calculate(uint64_t PacketNum) {
//             if (intransmissionmap(PacketNum)){
//                 return transmission_map.get_offset(PacketNum);
//             }
//             if (inretransmissionmap(PacketNum)){
//                 return retransmission_map.get_offset(PacketNum);
//             }
//             throw std::underflow_error("Error: packet doesn't belong to this block.");
//             return -1;
//         }

//         void ack4offset(uint64_t PacketNum, bool isReceived, uint32_t offset_ = 0){
//             auto packet_offset_ = offset_calculate(PacketNum);
//             if (isReceived){
//                 metabuf.acknowledege_and_drop(packet_offset_, true);
//             }else{
//                 metabuf.acknowledege_and_drop(packet_offset_, false);
//             }
//         }

//         uint8_t get_difference(){
//             return MetaDifference;
//         }

//         void set_buffer(struct iovec* iovecs, int iovecs_len, size_t type_, const uint8_t difference_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
//             if (difference_flag != std::numeric_limits<uint16_t>::max()){
//                 std::cerr << "MetaInfo set_buffer error(difference_flag(" << (int)difference_flag << "), (" << (int)difference_ << "))" << std::endl;
//                 _Exit(0);
//             }
//             difference_flag = difference_;
//             if (difference_flag != MetaDifference){
//                 std::cerr << "difference_flag(" << (int)difference_flag << "),MetaDifference(" << (int)MetaDifference << ")" << std::endl;
//                 _Exit(0);
//             }
//             metabuf.add_Meta(iovecs, iovecs_len);
//             range_len = 0;
//             send_len = 0;
//             block_type = type_;
//             for (auto i = 0; i < iovecs_len; i++){
//                 range_len += (iovecs[i].iov_len + MAX_SEND_UDP_PAYLOAD_SIZE - 1) / MAX_SEND_UDP_PAYLOAD_SIZE;
//             }
//         }

//         bool iscomplete(){
//             return metabuf.written_complete();
//         }

//         size_t sent(){
//             return metabuf.sentComplete();
//         }

//         void clear(){
//             transmission_map.clear();
//             retransmission_map.clear();
//             metabuf.clear();
//             block_type = std::numeric_limits<size_t>::max();
//             difference_flag = std::numeric_limits<uint16_t>::max();
//             range_len = 0;
//             send_len = 0;
//         }

//         void MetaInfo_log(bool contain_ = false) const{
//             std::cout << "{";
//             std::cout << "\"class\": \"MetaInfo\", ";
//             std::cout << "\"MetaDifference\": \"" << (int)MetaDifference << "\", ";
//             std::cout << "\"block size\": " << range_len << ", ";
//             std::cout << "\"block_type\": " << block_type << ", ";
//             std::cout << "\"address\": ";
//             if (contain_){
//                 metabuf.to_json();
//             }
//             std::cout << "}";   
//         }
// };

class MetaInfo{
    public:
        SendBuf metabuf;

        const uint8_t MetaDifference;

        MapSet<ReTransmissionMap> retransmission_map;

        std::vector<uint32_t> priority_offset;

        MapSet<TransmissionMap> transmission_map;

        uint16_t difference_flag = std::numeric_limits<uint16_t>::max();

        size_t range_len;

        size_t send_len;

        size_t block_type = std::numeric_limits<size_t>::max();

        std::pair<size_t, size_t> packet_range{std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};

        MetaInfo(size_t difference_flag_ = std::numeric_limits<uint16_t>::max()): 
        MetaDifference(difference_flag_),
        metabuf(MAX_SEND_UDP_PAYLOAD_SIZE),
        range_len(0),
        send_len(0){};

        ~MetaInfo(){};

        void add_transmission(uint64_t packetnum_, uint32_t packetoffset_){
            transmission_map.add(packetnum_, packetoffset_);
        }

        void add_retransmission(uint64_t packetnum_, uint32_t packetoffset_){
            retransmission_map.add(packetnum_, packetoffset_);
        }

        void removeoldmap(Packet_num_len packet_){
            retransmission_map.removeBeforeValue(packet_);
            transmission_map.removeBeforeValue(packet_);
        }

        void reset_packet_range(){
            packet_range = std::make_pair(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
        }

        bool no_overlap(const std::pair<uint64_t, uint64_t>& p1, const std::pair<uint64_t, uint64_t>& p2) {
            return p1.second < p2.first || p2.second < p1.first;
        }

        std::pair<uint64_t, uint64_t> get_packet_range(Packet_num_len packet_){
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

        size_t offset_calculate(uint64_t PacketNum) {
            auto result = transmission_map.get_offset(PacketNum);
            if (result != LIMIT_UINT32_T){
                // std::cout<<(int)MetaDifference<<", PacketNum:"<<PacketNum<<", "<<result;
                return result;
            }
            result = retransmission_map.get_offset(PacketNum);
            if (result != LIMIT_UINT32_T){
                return result;
            }
            throw std::underflow_error("Error: packet doesn't belong to this block.");
        }


        void ack4offset(uint64_t PacketNum, bool isReceived, uint32_t offset_ = 0){
            auto packet_offset_ = offset_calculate(PacketNum);
            /*Add priority calculation to logit remove "complete" data*/
            if (isReceived){
                // std::cout<<" ";
                metabuf.acknowledege_and_drop(packet_offset_, true);
            }else{
                metabuf.acknowledege_and_drop(packet_offset_, false);
            }
        }

        uint8_t get_difference(){
            return MetaDifference;
        }

        void set_buffer(struct iovec* iovecs, int iovecs_len, size_t type_, const uint8_t difference_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
            if (difference_flag != std::numeric_limits<uint16_t>::max()){
                std::cerr << "MetaInfo set_buffer error(difference_flag(" << (int)difference_flag << "), (" << (int)difference_ << "))" << std::endl;
                _Exit(0);
            }
            difference_flag = difference_;
            if (difference_flag != MetaDifference){
                std::cerr << "difference_flag(" << (int)difference_flag << "), MetaDifference(" << (int)MetaDifference << ")" << std::endl;
                _Exit(0);
            }
            metabuf.add_Meta(iovecs, iovecs_len);
            range_len = 0;
            send_len = 0;
            block_type = type_;
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
            transmission_map.clear();
            retransmission_map.clear();
            metabuf.clear();
            block_type = std::numeric_limits<size_t>::max();
            difference_flag = std::numeric_limits<uint16_t>::max();
            range_len = 0;
            send_len = 0;
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

        uint8_t lastest_difference;

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
            lastest_difference = (lastest_difference + 1) % get_capacity();
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

        std::pair<Packet_num_len, Packet_num_len> get_packet_range(Difference_len difference_, Packet_num_len pn_){
            return data_[difference_].get_packet_range(pn_);
        }

        void ack4offset(Difference_len difference_, Packet_num_len pn_, bool value_){
            data_[difference_].ack4offset(pn_, value_);
        }

        void add_transmission(Difference_len difference_, Packet_num_len pn_, Offset_len off_){
            data_[difference_].add_transmission(pn_, off_);
        }

        void add_retransmission(Difference_len difference_, Packet_num_len pn_, Offset_len off_){
            data_[difference_].add_retransmission(pn_, off_);
        }

        size_t get_status(Difference_len difference_){
            return data_[difference_].metabuf.get_status();
        }

        bool iscomplete(Difference_len difference_){
            return data_[difference_].iscomplete();
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

        bool iscomplete_check(uint8_t difference_){
            if (!inrangecheck(difference_)){
                std::cerr << "difference_("<<(int)difference_<<") not in range("<< head_ << ", " << tail_ <<")" << std::endl;
                _Exit(0);
            }
            return data_[difference_].iscomplete();
        }

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


        ~SCircularQueue() = default; 
};

/*Record copy contiouns*/
class RecordInfo
{
    private:
        /* data */
        uint8_t record_diffference = 0;

        uint32_t record_acumulate = 0;

        size_t end_index = std::numeric_limits<size_t>::max();

        size_t start_index = std::numeric_limits<size_t>::max();

        Offset_len record_offset = 0;
    public:
        RecordInfo(/* args */){};
        ~RecordInfo(){};
        void set(uint16_t index_, Offset_len len_, Offset_len offset_, Difference_len difference_){
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

        void update(uint32_t index_, size_t offset_, size_t len_, uint8_t difference_){
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

        uint8_t get_record_difference(){
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

        uint8_t rdifference;

        size_t received = 0;

        size_t processd = 0;

        size_t expected = 0;

        bool has_zero = false;

        size_t srcset = 0;

        metarecebuf(uint8_t difference_ = 0): rdifference(difference_){
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
            for (auto &e:source_len){
                e = 0;
            }
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
            for(auto const e:source_len){
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

        void processdlen(size_t len_){
            processd += len_;
        }

        void processCheck(){
            /*offset 0 packet didn't received, not do any operation*/
            if (srcset == 0){
                return;
            }
            if (received != processd){
                std::cout<<"received:"<<received<<", processd:"<<processd<<std::endl;
                throw std::overflow_error("received != processd");
            }
            return;
        }

        bool processComplete(){
            return processd == expected;
        }

        void copy(size_t offset_, void * src, size_t copy_len){
            if(!metabuf.src){
                std::cout<<"[metarecebuf copy()] "<<(void*)metabuf.src<<std::endl;
                _Exit(0);
            }
            memcpy(reinterpret_cast<uint8_t*>(metabuf.src + offset_), reinterpret_cast<uint8_t*>(src), copy_len);
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
};  

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
        for (size_t i = 0; i < capacity_; i++) {
            data_.push_back(i);
        }
    }

    size_t get_capacity(){
        return capacity_;
    }

    bool push_back() {
        if (count_ == capacity_){
            return false;
        }
        data_[tail_].clear();

        tail_ = (tail_ + 1) % capacity_;

        ++count_;

        return true;
    }

    void pop_front() {
        if (empty()) {
            throw std::runtime_error("Queue is empty, cannot remove element.");
        }
        data_[head_].clear();
        head_ = (head_ + 1) % capacity_;
        --count_;
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

    size_t start() const {
        return head_;
    }

    void insert(uint8_t difference, uint64_t pkt_offset, uint32_t pkt_length) {
        if (difference >= capacity_) {
            throw std::out_of_range("insert: difference index out of range");
        }
        data_[difference].find(pkt_offset, pkt_length);
    }

    bool iscomplete(uint8_t difference) const {
        if (difference >= capacity_) {
            throw std::out_of_range("iscomplete: difference index out of range");
        }
        return data_[difference].is_complete();
    }

    void set_recv_pointer(uint8_t difference, uint8_t* src) {
        if (difference >= capacity_) {
            throw std::out_of_range("set_recv_pointer: difference index out of range");
        }
        data_[difference].set_src(src);
    }

    void rx_len(uint8_t difference, size_t expected) {
        if (difference >= capacity_) {
            throw std::out_of_range("rx_len: difference index out of range");
        }
        data_[difference].addexplen(expected);
    }


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

    bool inrangecheck(uint8_t index) {
        bool inRange = false;
        if (head_ < tail_) {
            inRange = (index >= head_ && index < tail_);
        } else {
            inRange = (index >= head_ || index < tail_);
        }
        
        if (!inRange) {
            void* caller_address = __builtin_return_address(0);
            Dl_info info;

            if (dladdr(caller_address, &info) && info.dli_sname) {
                std::cerr << "Function '" << info.dli_sname << "' called inrangecheck, but the result is false." << std::endl;       
            } else {
                std::cerr << "Unknown caller function." << std::endl;
            }
            _Exit(0);
        }

        return inRange;
    }

    bool processComplete(uint8_t difference_){
        return data_[difference_].processComplete();
    }

    /*Copy data*/
    void copy (uint8_t difference_, Offset_len offset_, void * src_, size_t len_){
        // if (!inrangecheck(difference_)){
        //     std::cerr << "difference_("<<(int)difference_<<") not in range("<< head_ << ", " << tail_ <<")" << std::endl;
        //     _Exit(0);
        // }
        inrangecheck(difference_);
        /*Merge copy() and processdlen() into merge()*/
        data_[difference_].copy(offset_, src_, len_);
        data_[difference_].processdlen(len_);
        data_[difference_].record_copy(offset_);
    }

    bool copyed_check(uint8_t difference_, Offset_len offset_){
        return data_[difference_].copyed_check(offset_);
    }

    /*Check difference_ block is received*/
    void processCheck(uint8_t difference_){
        // if (!inrangecheck(difference_)){
        //     std::cerr << "processCheck difference_("<<(int)difference_<<") not in range("<< head_ << ", " << tail_ <<")" << std::endl;
        //     _Exit(0);
        // }
        inrangecheck(difference_);
        data_[difference_].processCheck();
    }

    /*Check data pointer is available*/
    bool targetCheck(uint8_t difference_){
        inrangecheck(difference_);
        return data_[difference_].targetCheck();
    }

    /*Check OP is available*/
    bool srcsetcheck(uint8_t difference_){
        inrangecheck(difference_);
        return data_[difference_].srcsetCheck();
    }

    ~RCircularQueue() = default;
};

/*Receive queue registation*/
// class zeroQueue {
// private:
//     using PairType = std::pair<uint8_t, uint16_t>;
//     std::vector<PairType> data;  
//     int front_index;        
//     int back_index;         
//     int count;              
//     int capacity;           

//     void resize() {
//         std::vector<PairType> new_data(capacity * 2);
//         for (int i = 0; i < count; ++i) {
//             new_data[i] = (*this)[i];  
//         }
//         data = std::move(new_data);
//         front_index = 0;
//         back_index = count;
//         capacity *= 2;
//     }

// public:
//     zeroQueue(int cap = DataBlock) : front_index(0), back_index(0), count(0), capacity(cap) {
//         data.resize(capacity);
//     }

//     void push_back(PairType value) {
//         if (count == capacity) {
//             resize();  
//         }
//         data[back_index] = value;
//         back_index = (back_index + 1) % capacity;
//         count++;
//     }

//     void pop_front() {
//         if (count == 0) {
//             throw std::runtime_error("zeroQueue is empty!");
//         }
//         front_index = (front_index + 1) % capacity;
//         count--;
//     }

//     PairType& operator[](int index) {
//         if (index < 0 || index >= count) {
//             std::cout<<"index"<<index<<std::endl;
//             throw std::out_of_range("zeroQueue index out of range");
//         }
//         return data[(front_index + index) % capacity];  
//     }

//     int size() const {
//         return count;
//     }

//     bool empty() const {
//         return count == 0;
//     }

//     PairType front() const {
//         if (count == 0) throw std::runtime_error("zeroQueue front() is empty!");
//         return data[front_index];
//     }

//     PairType back() const {
//         if (count == 0) throw std::runtime_error("zeroQueue back() is empty!");
//         return data[(back_index - 1 + capacity) % capacity];
//     }
// };

class zeroQueue {
public:
    using PairType = std::pair<uint8_t, uint16_t>;

private:
    std::vector<PairType> data;
    int front_index{0};
    int back_index{0};
    int count{0};
    int capacity{DataBlock};

    void resize() {
        std::vector<PairType> new_data(capacity * 2);
        for (int i = 0; i < count; ++i) {
            new_data[i] = (*this)[i];
        }
        data = std::move(new_data);
        front_index = 0;
        back_index = count;
        capacity *= 2;
    }

    int mod_add(int x, int d) const { return (x + d + capacity) % capacity; }
    int mod_sub(int x, int d) const { return (x - d % capacity + capacity) % capacity; }

public:
    zeroQueue(int cap = DataBlock)
      : capacity(cap)
    {
        data.resize(capacity);
        for (auto i = 0; i < data.size(); i++){
            data[i] = std::make_pair(LIMIT_UINT8_T, LIMIT_UINT16_T);
        }
    }

    void push_back(uint8_t idx, uint16_t payload) {
        if (count == capacity) {
            resize();
        }

        int target = idx % capacity;
        int dist_back = (target - back_index + capacity) % capacity;
        int dist_front = (front_index - target + capacity) % capacity;

        if (dist_back <= dist_front) {
            back_index = target;
            data[back_index] = { idx, payload };
            back_index = mod_add(back_index, 1);
        } else {
            front_index = mod_sub(front_index, dist_front);
            data[front_index] = { idx, payload };
        }

        ++count;
    }

    void pop_front() {
        if (count == 0) throw std::runtime_error("zeroQueue is empty!");
        front_index = mod_add(front_index, 1);
        --count;
    }

    PairType& operator[](int logical_idx) {
        if (logical_idx < 0 || logical_idx >= count)
            throw std::out_of_range("zeroQueue index out of range");
        return data[mod_add(front_index, logical_idx)];
    }

    int size() const { return count; }
    bool empty() const { return count == 0; }

    PairType front() const {
        if (count == 0) throw std::runtime_error("zeroQueue front() is empty!");
        if (front_index != data[front_index].first){
            throw std::runtime_error("1 front_index != data[front_index].first!");
        }
        return data[front_index];
    }

    PairType back() const {
        if (count == 0) throw std::runtime_error("zeroQueue back() is empty!");
        if (mod_sub(back_index, 1) != data[mod_sub(back_index, 1)].first){
            throw std::runtime_error("1 back_index != data[back_index].first!");
        }
        return data[mod_sub(back_index, 1)];
    }


    int indices_used() const {
        if (count == 0) return 0;
        auto f = front().first;
        auto b = back().first;
        return static_cast<int>(b) - static_cast<int>(f) + 1;
    }
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

    // map for received application pktnum and corresponding offset
    std::vector<uint8_t> receivevector;

    RCset receive_offset;

    std::vector<uint8_t> acknowldge_header;

    struct msghdr acknowldge_msghdr;

    std::vector<struct iovec> acknowldge_iov;  

    size_t send_packet_type = 0;

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

    RecvBuf rec_buffer;

    size_t current_buffer_pos;

    bool initial;

    // total data sent after one get data.
    size_t written_data_once;

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

    uint8_t receive_connection_difference_registration;

    size_t current_loop_min;

    size_t current_loop_max;

    size_t send_status_flag;

    /*
    ts_record is used to calculate approximate send ts for each packet.
    Approximate ts ~= (2nd ts - 1st ts)/(2nd pkt - 1st pkt + 1)
    */ 
    std::pair<std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>, std::pair<uint64_t, std::chrono::high_resolution_clock::time_point>> ts_record;

    std::chrono::high_resolution_clock::time_point start_ts;

    std::chrono::high_resolution_clock::time_point end_ts;
    
    size_t normal_initial = 0;

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

    zeroQueue zerolist;

    RCircularQueue recvCQ;

    RecordInfo receive_record;

    /*Avoid multiple cwnd reduction in same tramsmission round*/
    bool first_loss;

    Packet_num_len max_acknowleged = LIMIT_UINT64_T;

    Connection(sockaddr_storage local, sockaddr_storage peer, bool server):    
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
    receive_connection_difference_registration(LIMIT_UINT8_T),
    current_loop_min(0),
    current_loop_max(0),
    recovery(MAX_SEND_UDP_PAYLOAD_SIZE),
    difference_flag(false),
    send_status_flag(0),
    acknowldge_iov(3, {nullptr, 0}),
    receivevector(MAX_SEND_UDP_PAYLOAD_SIZE, 0),
    receive_offset(MAX_SEND_UDP_PAYLOAD_SIZE),
    rx_buffer(MAX_SEND_UDP_PAYLOAD_SIZE * RX_CONST, 0),
    receive_available_map(RX_CONST, 0),
    first_loss(false)
    {
        memset(&acknowldge_msghdr, 0, sizeof(acknowldge_msghdr));
        acknowldge_msghdr.msg_iov = nullptr;
        acknowldge_msghdr.msg_iovlen = 0;

        send_message.resize(ONCE_SEND_LIMIT);

        receive_message.resize(RX_CONST);

        receive_offset.add_rule(1);

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
        auto arrive_time = std::chrono::high_resolution_clock::now();
        srtt = arrive_time - handshake;
        rttvar = srtt / 2;
        rto = srtt + 4 * rttvar;
    }

    void update_rtt(std::chrono::high_resolution_clock::time_point send_time, std::chrono::high_resolution_clock::time_point receive_time){
        rtt = receive_time - send_time;
        auto tmp_srtt = std::chrono::duration<double, std::nano>(srtt.count() * alpha + (1 - alpha) * rtt.count());
        srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_srtt);
        auto diff = srtt - rtt;
        auto tmp_rttvar = std::chrono::duration<double, std::nano>((1 - beta) * rttvar.count() + beta * std::abs(diff.count()));
        rttvar = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_rttvar);
        rto = srtt + 4 * rttvar;
        std::cout<<"RTO:"<<rto.count()<<", "<<tmp_rttvar.count()<<", srr:"<<srtt.count()<<", rtt:"<<std::chrono::duration_cast<std::chrono::nanoseconds>(rtt).count()<<std::endl;
    }

    // void update_rtt() {
    //     /*
    //     handshake_confirmed
    //     handshake_complete
        
    //     1st RTO:
    //     SRTT <- R
    //     RTTVAR <- R/2
    //     RTO <- SRTT + max (G, K*RTTVAR)
    //     where K = 4.

    //     RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
    //     SRTT <- (1 - alpha) * SRTT + alpha * R'
    //     RTO <- SRTT + max (G, K*RTTVAR)
    //     */
    //     auto arrive_time = std::chrono::high_resolution_clock::now();
    //     rtt = arrive_time - handshake;    
    //     auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(arrive_time.time_since_epoch()).count();
    //     auto tmp_srtt = std::chrono::duration<double, std::nano>(srtt.count() * alpha + (1 - alpha) * rtt.count());
    //     srtt = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_srtt);
    //     auto diff = srtt - rtt;
    //     auto tmp_rttvar = std::chrono::duration<double, std::nano>((1 - beta) * rttvar.count() + beta * std::abs(diff.count()));
    //     rttvar = std::chrono::duration_cast<std::chrono::nanoseconds>(tmp_rttvar);
    //     if (srtt.count() < minrtt.count()){
    //         minrtt = srtt;
    //     }
    //     rto = srtt + 4 * rttvar;
    // };

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
        for (auto i = 0 ; i <= receive_max_index ; i++){
            if (receive_available_map[i] == 1)
            {
                continue;
            }
            auto pkt_ty = receive_message[i].get_packet_type();
            
            if (pkt_ty == Type::ACK){
                process_acknowledge(i);
            }

            if (pkt_ty == Type::Application){
                process_application_packet(i);
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
        // if (pkt_ty == Type::ACK){
        //     process_acknowledge(src, src_len, ts);
        // }

        // if (pkt_ty == Type::Application){
        //     process_application_packet(src, src_len);                 
        // }

        // if (pkt_ty == Type::Stop){
        //     stop_flag = false;
        //     return 0;
        // }

        return read_;
    };

    /*
    TODO（3/2）:
    1. skip pakcet older than this round minimum packet
    2. add new flag to shrink receive message queue.
    */
    void process_application_packet(size_t index){
        Packet_num_len pkt_num = receive_message[index].get_packet_number();
        Offset_len pkt_offset = receive_message[index].get_packet_offset();
        Difference_len pkt_difference = receive_message[index].get_packet_difference();
        auto pkt_length = receive_message[index].get_packet_length();

        // std::cout<<(int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<std::endl;
        /* no operation on old packet*/
        if (pkt_num < current_loop_min){
            return;
        }
        recvCQ.indexcheck(pkt_difference);
        /*index is not availble*/
        receive_available_map[index] = 1;
        /*TODO(2.24): break index, new parameter: index_check*/

        if (pkt_offset == 0){
            zerolist.push_back(pkt_difference, index);
            std::cout<<(int)pkt_difference<<", pkt_num:"<<pkt_num <<", current_loop_min:"<<current_loop_min<<", pkt_offset:"<<pkt_offset<<std::endl;
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
        // if (ssize_t(pkt_num) > max_received && ){
        //     max_received = ssize_t(pkt_num);
        //     /* bit map substitude byte map*/
        //     send_num = pkt_num;
        // }    

        
        size_t pos = pkt_num - current_loop_min;
        size_t byte_index = pos / 8;
        size_t bit_index = pos % 8;

        if (byte_index > receivevector.size()){
            std::cerr << "Error: Bit position out of range. (byte_index:"<< byte_index <<", "<< receivevector.size() 
            <<", "<<max_received<<", "<< current_loop_min <<")" << std::endl;
            _Exit(0);
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


    // void process_application_packet(uint8_t* src, size_t src_len){
        
    // };
    
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

        ACKrange = current_loop_min;
        acknowldge_iov[1].iov_base = &ACKrange;
        acknowldge_iov[1].iov_len = sizeof(uint64_t);

        acknowldge_iov[2].iov_base = receivevector.data();
        acknowldge_iov[2].iov_len = info_len;

        acknowldge_msghdr.msg_iov = &acknowldge_iov[0];
        acknowldge_msghdr.msg_iovlen = 3;

        std::cout<<"send_acknowledge:"<<send_num<<", "<<ACKrange<<", "<<max_received<<std::endl;

        send_packet_type = ty;
        return sizeof(Header) + hdr->pkt_length;
    }
    

    bool check_status(){
        std::cout << "check_status cwnd left:" << recovery.cwnd_available() <<", "<<recovery.cwnd_enough()<<", "<<sendbufferqueue.ready()<<std::endl;
        if (recovery.cwnd_enough() && sendbufferqueue.ready()) return true;
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


    void process_acknowledge(const size_t index_){
        auto pkt_num = receive_message[index_].get_packet_number();
        auto pkt_len = receive_message[index_].get_packet_length();
        auto pkt_difference = receive_message[index_].get_packet_difference();
        receive_available_map[index_] = 0;
       
        auto receivets = std::chrono::high_resolution_clock::now();
        auto ackts = tsInfo.removeBeforeValue(pkt_num);
        update_rtt(ackts, receivets);

        auto first_pn = *reinterpret_cast<const uint64_t*>(receive_message[index_].iov[1].iov_base);
        auto end_pn = pkt_num;
        bool loss = false;
        size_t total_send = end_pn - first_pn + 1;
        auto ack_src = reinterpret_cast<const uint8_t*>(receive_message[index_].iov[1].iov_base) + sizeof(uint64_t);
        size_t byte_index = 0;
        size_t bit_index = 0;

        
        /*TODO: process max_ack and first_pn*/
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        auto pn = first_pn;
        std::cout<<"first_pn:"<<first_pn<<", end_pn:"<<end_pn<<", "<<pkt_len<<", "<<max_acknowleged<<std::endl;

        /*Check acknowledge packet loss*/
        if (first_pn != (max_acknowleged + 1)){
            auto loss_pn = max_acknowleged + 1;
            while (true)
            {
                if (loss_pn == first_pn){
                    break;
                }
                size_t i = 0;
                std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
                for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                    i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                    sendpair = sendbufferqueue.get_packet_range(i, loss_pn);
                    if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
                        continue;
                    }
                    if (loss_pn <= sendpair.second && loss_pn >= sendpair.first){
                        break;
                    }
                }
                if (i == 0 && (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T))){
                    std::cerr << "Acknowledge unknow packet(" << loss_pn << ")" << std::endl;
                    _Exit(0);
                }
                auto compare_ = sendbufferqueue.compareIndices(i, pkt_difference);
                if (compare_ == 0){
                    while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
                        sendbufferqueue.ack4offset(i, loss_pn, true);
                        loss_pn++;
                    }
                }else{
                    while (loss_pn >= sendpair.first && loss_pn <= sendpair.second){
                        sendbufferqueue.ack4offset(i, loss_pn, false);
                        loss_pn++;
                    }
                }
                
            }
        }
        std::cout<<"process_acknowledge 1"<<std::endl;
        // for (auto i = sendbufferqueue.start(); i < sendbufferqueue.end(); i = (i + 1) % 256){
        while (true)
        {
            if (pn > end_pn){
                break;
            }
            
            size_t i = 0;
            std::pair<Packet_num_len, Packet_num_len> sendpair = {LIMIT_UINT64_T, LIMIT_UINT64_T};
            for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++){
                i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
                // sendpair = sendbufferqueue.data_[i].get_packet_range(pn);
                sendpair = sendbufferqueue.get_packet_range(i, pn);
                if (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T)){
                    continue;
                }
                if (pn <= sendpair.second && pn >= sendpair.first){
                    break;
                }
            }
            // std::cout<<(int)i<<", sendpair:" << sendpair.first << ", " << sendpair.second <<std::endl;
            if (i == 0 && (sendpair == std::make_pair(LIMIT_UINT64_T, LIMIT_UINT64_T))){
                std::cerr << "Acknowledge unknow packet(" << pn << ")" << std::endl;
                _Exit(0);
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
                // std::cout<<"process_acknowledge 4"<<std::endl;
            }

        }
        std::cout<<"process_acknowledge 2"<<std::endl;
        max_acknowleged = end_pn;
        
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
                std::cout << int(index) << " " ;
                sendbufferqueue.data_[index].metabuf.ack_check();
            }
        }


    }


    void clear_recv_setting(){
        receive_offset.clear();
    }

    void recv_reset(){
        rec_buffer.reset();
    }

    void update_receive_difference(){
        receive_connection_difference = (receive_connection_difference + 1) % DataBlock;
    }

     bool receive_complete(){
        if (receive_connection_difference == recvCQ.start() && !recvCQ.empty()){
            return recvCQ.iscomplete(receive_connection_difference);
        }
        return false;
    }

    /*zerolist cannt be the different check*/
    // bool zerocheck(){       
    //     if (!zerolist.empty()){
    //         auto len_ = zerolist.size();
    //         for (auto i = 0; i < len_; i++){
    //             if (receive_connection_difference == zerolist[i].first)
    //             {
    //                 return true;
    //             }
                
    //         }
    //     }
       
    //     return false;
    // }

    // void rx_len(size_t expected){
    //     recvCQ.rx_len(receive_connection_difference, expected);
    // }

    // void get_recv_target(uint8_t * target_){
    //     /*check top poiner is null or not*/
    //     recvCQ.set_recv_pointer(receive_connection_difference, target_);
    // }


    bool zerocheck(){
        if (!zerolist.empty()){
            if(receive_connection_difference == zerolist[0].first){
                return true;
            }
        }
       
        return false;
    }

    void rx_len(size_t expected){
        recvCQ.rx_len(zerolist[0].first, expected);
    }

    void get_recv_target(uint8_t * target_){
        /*check top poiner is null or not*/
        recvCQ.set_recv_pointer(zerolist[0].first, target_);
    }

    void reset_rx_len(){
        rx_length = 0;
    }

    void set_send_time(){
        handshake = std::chrono::high_resolution_clock::now();
    }
    

    bool get_data(struct iovec* iovecs, int iovecs_len, int type_, const std::vector<std::vector<uint8_t>> &priotity_list = {}){
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

        sendbufferqueue.push_back(iovecs, iovecs_len, type_, priotity_list);
        
        recovery.bytes_in_flight = 0;
        set_handshake();
        return completed;
    }

    size_t get_once_data_len(){
        return written_data_once;
    }

    void clear_sent_once(){
        written_data_once = 0;
    }

    void process_timeout(){

    }

    // ssize_t prepareData() {
    //     Type ty = Type::Application;
    //     ssize_t out_len = 0; 
    //     Offset_len out_off = 0;
    //     size_t i = 0;
    //     ssize_t tramssioning_index = -1;

    //     ssize_t cwnd_limit = (recovery.cwnd_available() + MAX_SEND_UDP_PAYLOAD_SIZE - 1) / MAX_SEND_UDP_PAYLOAD_SIZE;
    //     if (cwnd_limit <= 0){
    //         return 0;
    //     }

    //     size_t sent_limit = std::min(send_message.size(), (size_t)cwnd_limit);
    //     ssize_t sent = 0;        
    //     for (i = sendbufferqueue.start() ; i < sendbufferqueue.end() ; i = (i + 1)%sendbufferqueue.max()) {
    //         while (true){
    //             size_t send_status = sendbufferqueue.data_[i].metabuf.get_status();
    //             auto s_flag = sendbufferqueue.data_[i].metabuf.emit(send_message[sent].iov[1], out_len, out_off, send_status);
    //             if (out_len < 0 || out_len > 1440){
    //                 std::cout<<"[Debug] difference:"<<i<<",out_len:"<<out_len<<", out_off:"<<out_off<<std::endl;
    //             }
    //             if (out_len == -1) {
    //                 break;
    //             }

    //             auto pn = pkt_num_spaces[0].updatepktnum();
    //             send_message[sent].setMessageHeader(pn, out_off, i, (Packet_num_len)out_len);
    //             recovery.on_packet_sent(out_len);
    //             // if (sendbufferqueue.data_[i].metabuf.meta_status == MetaFlag::Initial){
    //             //     sendbufferqueue.data_[i].add_transmission(pn, out_off);
    //             // }else if(sendbufferqueue.data_[i].metabuf.meta_status == MetaFlag::Retransmission){
    //             //     sendbufferqueue.data_[i].add_retransmission(pn, out_off);
    //             // }
    //             if (send_status == 1){
    //                 sendbufferqueue.data_[i].add_transmission(pn, out_off);
    //             }else if(send_status == 2){
    //                 sendbufferqueue.data_[i].add_retransmission(pn, out_off);
    //             }
    //             sent++;
    //             if (sent >= sent_limit){
    //                 /*TODO add pakcet number-offset mapping*/
    //                 break;
    //             }
    //         }
    //         if (sent >= sent_limit){
    //             break;
    //         }
    //     }

    //     return sent;
    // }

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
        auto sendbufferqueue_start_index = sendbufferqueue.start();
        for (auto idx = 0; idx < sendbufferqueue.get_count(); idx++) {
            i = (sendbufferqueue_start_index + idx) % sendbufferqueue.get_capacity();
            int d_sent = 0;
            while (true){
                // size_t send_status = sendbufferqueue.data_[i].metabuf.get_status();
                size_t send_status = sendbufferqueue.get_status(i);
                auto s_flag = sendbufferqueue.data_[i].metabuf.emit(send_message[sent].iov[1], out_len, out_off);
                
                if (out_len == -1) {
                    break;
                }

                auto pn = pkt_num_spaces[0].updatepktnum();
                if (out_off == 0 && out_len > -1){
                    std::cout<<"[Debug] difference:"<< i <<", pn:"<< pn <<", out_len:"<<out_len<<", out_off:"<<out_off<<std::endl;
                }
                send_message[sent].setMessageHeader(pn, out_off, i, (Packet_num_len)out_len);
                recovery.on_packet_sent(out_len);
                
                if (send_status == 1){
                    // sendbufferqueue.data_[i].add_transmission(pn, out_off);
                    sendbufferqueue.add_transmission(i, pn, out_off);
                }else if(send_status == 2){
                    sendbufferqueue.add_retransmission(i, pn, out_off);
                }
                sent++;
                if (sent >= sent_limit){
                    // std::cout<<"last pn:"<<pn<<", send_status:" <<send_status<<std::endl;
                    /*TODO add pakcet number-offset mapping*/
                    break;
                }
                d_sent++;
            }
            // std::cout<<"prepareData:"<<(int)i<<", "<<d_sent<<std::endl;
            if (sent >= sent_limit){
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
    void send_packet_complete(size_t err_ = 0, size_t sent = 0, const std::chrono::system_clock::time_point& start_ts = std::chrono::system_clock::time_point{}){
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
            /*
            For cache in receiver_cache
                cache.copy()
            */
            process_application_copy();
          
        }else if(send_packet_type == Type::Application){
            end_index = -1;
            set_handshake();
            tsInfo.updateQueue(send_message[start_index].get_packet_number(), start_ts, pkt_num_spaces[0].getpktnum(), end_ts);
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

    // old
    // void process_application_copy(){
    //     Offset_len pkt_offset;
    //     Packet_len pkt_len;
    //     Difference_len pkt_difference;
    //     if (!zerolist.empty()){
    //         if (receive_connection_difference == zerolist[0].first && recvCQ.data_[receive_connection_difference].srcset == 1){
    //             auto index = zerolist[0].second;
    //             pkt_offset = receive_message[index].get_packet_offset();
    //             pkt_difference = receive_message[index].get_packet_difference();
    //             recvCQ.data_[pkt_difference].copy((pkt_offset), receive_message[index].iov[1].iov_base, 48);
    //             // memcpy(recvCQ.data_[pkt_difference].metabuf.src, reinterpret_cast<uint8_t*>(receive_message[index].iov[1].iov_base), 48);
    //             receive_available_map[index] = 0;
    //             recvCQ.data_[pkt_difference].processdlen(48);
    //             receive_record.reset();
    //             return;
    //         }
    //     }
        

    //     /*TODO: used count to reduce iteration times*/
    //     for (auto index = 0; index < receive_available_map.size(); index++){
    //         pkt_difference = receive_message[index].get_packet_difference();
    //         if (receive_available_map[index] == 0){
    //             if (!receive_record.empty()){
    //                 auto copy_len = receive_record.get_acumulation();
    //                 auto copy_index = receive_record.get_start_index();
    //                 auto copy_difference = receive_record.get_record_difference();
    //                 auto copy_offset = receive_record.get_offset();
    //                 if (copy_offset >= 48){
    //                     recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(reinterpret_cast<uint8_t*>(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48), reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }else{
    //                     recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }
    //                 recvCQ.data_[copy_difference].processdlen(copy_len);
    //                 receive_record.reset();
    //             }   
    //             continue;
    //         }

    //         if (receive_connection_difference == pkt_difference && recvCQ.data_[pkt_difference].metabuf.src != nullptr){
    //             pkt_offset = receive_message[index].get_packet_offset();
    //             pkt_len = receive_message[index].get_packet_length();

    //             receive_available_map[index] = 0;

    //             if(receive_record.get_end_index() - receive_record.get_start_index() == 7){ 
    //                 auto copy_len = receive_record.get_acumulation();
    //                 auto copy_index = receive_record.get_start_index();
    //                 auto copy_offset = receive_record.get_offset();
    //                 if (copy_offset >= 48){
    //                     recvCQ.data_[pkt_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }else{
    //                     recvCQ.data_[pkt_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }
    //                 recvCQ.data_[pkt_difference].processdlen(copy_len);
    //                 receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
    //                 if(recvCQ.processComplete(pkt_difference)){
    //                     return;
    //                 }
    //                 continue;
    //             }

    //             if (receive_record.get_target_offset() != pkt_offset && receive_record.get_target_offset() != 0){
    //                 auto copy_len = receive_record.get_acumulation();
    //                 auto copy_index = receive_record.get_start_index();
    //                 auto copy_offset = receive_record.get_offset();
    //                 if (copy_offset >= 48){
    //                     recvCQ.data_[pkt_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }else{
    //                     recvCQ.data_[pkt_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }
    //                 recvCQ.data_[pkt_difference].processdlen(copy_len);
    //                 receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
    //                 if(recvCQ.processComplete(pkt_difference)){
    //                     return;
    //                 }
    //                 continue;
    //             }

    //             receive_record.update(index, pkt_offset, pkt_len, pkt_difference);
    //         }else{
    //             /*different block occurs, contious check stop and start copy*/
    //             receive_upper_check = index;
    //             if (!receive_record.empty()){
    //                 auto copy_len = receive_record.get_acumulation();
    //                 auto copy_index = receive_record.get_start_index();
    //                 auto copy_difference = receive_record.get_record_difference();
    //                 auto copy_offset = receive_record.get_offset();
    //                 if (pkt_offset >= 48){
    //                     recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }else{
    //                     recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
    //                     // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //                 }
    //                 recvCQ.data_[copy_difference].processdlen(copy_len);
    //                 receive_record.reset();
    //                 if (copy_difference != receive_connection_difference){
    //                     std::cerr << "[process_application_copy check 3]: copy_difference(" << (int)copy_difference<<"), receive_connection_difference(" << (int)receive_connection_difference<<")"<<std::endl;
    //                     _Exit(0);
    //                 }
    //                 if(recvCQ.processComplete(copy_difference)){
    //                     return;
    //                 }
    //             }  
    //         }
    //     }


    //     if (!receive_record.empty()){
    //         auto copy_len = receive_record.get_acumulation();
    //         auto copy_index = receive_record.get_start_index();
    //         auto copy_difference = receive_record.get_record_difference();
    //         auto copy_offset = receive_record.get_offset();
    //         if (copy_offset >= 48){
    //             recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
    //             // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset - 48, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //         }else{
    //             recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
    //             // memcpy(recvCQ.data_[pkt_difference].metabuf.src + pkt_offset, reinterpret_cast<uint8_t*>(receive_message[copy_index].iov[1].iov_base), copy_len);
    //         }
    //         recvCQ.data_[copy_difference].processdlen(copy_len);
    //         receive_record.reset();
    //     }   

    //     // recvCQ.data_[receive_record.get_record_difference()].processCheck();
    //     recvCQ.data_[receive_connection_difference].processCheck();

    //     receive_record.reset();
    //     /*
    //     1. Max 8 copy packets
    //     2. index contious breaks
    //     3. difference differ
    //     */
    //     /*
    //     if receive_connection_difference == pkt_difference & src
    //         if (end - start == 7)
    //             !receive_record.empty
    //                 copy(recordinfo)
    //                 receive_record.set(index, pkt_len)
    //             continue;
            
    //         if (receive_record.offset + packet_limit != pkt_offset)
    //             if !receive_record.empty
    //                 copy(record)
    //             continue;
    //         else
    //             receive_record.update(end_index, pkt_len);
    //     else
    //         if check receive_record is not empty; other block data interupt
    //             copy()
    //             reset()
    //         else
    //             continue;
        
    //     if(!receive_record.empty())
    //         copy(receive_record)
    //         receive_record.reset;
    //     */
       
    // }

    bool registration_check(){
        return receive_connection_difference == receive_connection_difference_registration;
    }

    // 3.27 recvCQ.data_ cannot be directly access
    void process_application_copy(){
        Offset_len pkt_offset;
        Packet_len pkt_len;
        Difference_len pkt_difference;
        if (!zerolist.empty()){
            // if (receive_connection_difference == zerolist[0].first && recvCQ.data_[receive_connection_difference].srcset == 1){
            if (receive_connection_difference == zerolist[0].first && recvCQ.srcsetcheck(receive_connection_difference)){
                auto index = zerolist[0].second;
                pkt_offset = receive_message[index].get_packet_offset();
                pkt_difference = receive_message[index].get_packet_difference();
                recvCQ.copy(pkt_difference, pkt_offset, receive_message[index].iov[1].iov_base, 48);
                // recvCQ.data_[pkt_difference].copy((pkt_offset), receive_message[index].iov[1].iov_base, 48);
                // memcpy(recvCQ.data_[pkt_difference].metabuf.src, reinterpret_cast<uint8_t*>(receive_message[index].iov[1].iov_base), 48);
                receive_available_map[index] = 0;
                // recvCQ.data_[pkt_difference].processdlen(48);
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
                        recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    // recvCQ.data_[copy_difference].processdlen(copy_len);
                    receive_record.reset();
                }   
                continue;
            }

            // if (receive_connection_difference == pkt_difference && recvCQ.data_[pkt_difference].metabuf.src != nullptr){
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
                            recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                            // recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                        }else{
                            recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                            // recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        }
                        // recvCQ.data_[copy_difference].processdlen(copy_len);
                        receive_record.reset();
                    }   
                    continue;
                }

                if(receive_record.get_end_index() - receive_record.get_start_index() == 7){ 
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        recvCQ.copy(pkt_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);                        
                        // recvCQ.data_[pkt_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(pkt_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[pkt_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    // recvCQ.data_[pkt_difference].processdlen(copy_len);
                    receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
                    if(recvCQ.processComplete(pkt_difference)){
                        return;
                    }
                    continue;
                }

                if (receive_record.get_target_offset() != pkt_offset && receive_record.get_target_offset() != 0){
                    auto copy_len = receive_record.get_acumulation();
                    auto copy_index = receive_record.get_start_index();
                    auto copy_offset = receive_record.get_offset();
                    if (copy_offset >= 48){
                        recvCQ.copy(pkt_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[pkt_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(pkt_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[pkt_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    // recvCQ.data_[pkt_difference].processdlen(copy_len);
                    receive_record.set(index, pkt_len, pkt_offset, pkt_difference);
                    if(recvCQ.processComplete(pkt_difference)){
                        return;
                    }
                    continue;
                }

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
                        recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }else{
                        recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                        // recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                    }
                    // recvCQ.data_[copy_difference].processdlen(copy_len);
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
                recvCQ.copy(copy_difference, (copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
                // recvCQ.data_[copy_difference].copy((copy_offset - 48), receive_message[copy_index].iov[1].iov_base, copy_len);
            }else{
                recvCQ.copy(copy_difference, (copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
                // recvCQ.data_[copy_difference].copy((copy_offset), receive_message[copy_index].iov[1].iov_base, copy_len);
            }
            // recvCQ.data_[copy_difference].processdlen(copy_len);
            receive_record.reset();
        }   

        // recvCQ.data_[receive_connection_difference].processCheck();
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
        // if (sendbufferqueue.data_[sendbufferqueue.start()].iscomplete()){
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

        /*
        If there exists EAGAIN, resend message first

        return
        */

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
            /*
            send_acknowledge(src, size(src));
            */
            // If acknowledge too long, just 1000 acknowledge lastest part.
            // if(receive_result.size() > 1200){
            //     psize = 1200 + 2 * sizeof(uint64_t);
            // }else{
            //     psize = (uint64_t)(receive_result.size()) + 2 * sizeof(uint64_t);
            // }
            
            // Header* hdr = new Header(ty, send_num, 0, receive_connection_difference, psize);
            // if (difference_flag){
            //     uint8_t tmp = receive_connection_difference - 1;
            //     hdr->difference = tmp;
            // }

            // // Recevie info clear.

            // memcpy(out, hdr, HEADER_LENGTH);
            // if(receive_range.second - receive_range.first <= 1199){
            //     memcpy(out + HEADER_LENGTH, &receive_range.first, sizeof(uint64_t));
            //     memcpy(out + HEADER_LENGTH + sizeof(uint64_t), &receive_range.second, sizeof(uint64_t));
            //     memcpy(out + HEADER_LENGTH + 2 * sizeof(uint64_t), receive_result.data(), receive_result.size());
            // }else{
            //     // memcpy(out + HEADER_LENGTH, &receive_range.first, sizeof(uint64_t));
            //     auto first_pn = receive_range.second - 1199;
            //     auto range_offset = first_pn - receive_range.first;
            //     memcpy(out + HEADER_LENGTH, &first_pn, sizeof(uint64_t));
            //     memcpy(out + HEADER_LENGTH + sizeof(uint64_t), &receive_range.second, sizeof(uint64_t));
            //     memcpy(out + HEADER_LENGTH + 2 * sizeof(uint64_t), receive_result.data() + range_offset, 1200);
            // }
            
            // receive_result.clear();
            // difference_flag = false;
            // delete hdr; 
            // hdr = nullptr; 
            // update_receive_parameter();
        }      

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

    size_t read(uint8_t* out, bool iscopy, size_t output_len = 0){
        return rec_buffer.emit(out, iscopy, output_len);
    };

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