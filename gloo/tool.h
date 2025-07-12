#pragma once
#include <chrono>
#include <iostream>
#include <optional>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <omp.h>
#include <thread>
// #include <span>
// #include <execution>
#include <immintrin.h>
#include <cassert>
#include "packet.h"
namespace dmludp {
    inline constexpr size_t HEADER_LENGTH = sizeof(Header);

    // The default max_datagram_size used in congestion control.
    inline constexpr size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1440;

    inline constexpr size_t MAX_ACK_UDP_PAYLOAD_SIZE = 1400;

    inline constexpr size_t RX_CONST = 8192;

    inline constexpr size_t MAP_CONST = 65536;

    inline constexpr size_t ONCE_LIMIT = 1300;

    inline constexpr size_t ONCE_SEND_LIMIT = ONCE_LIMIT;

    inline constexpr size_t ONCE_RECEIVE_LIMINT = ONCE_LIMIT;

    inline constexpr double alpha = 0.875;

    inline constexpr double beta = 0.25;

    inline constexpr size_t DataBlock = 32;

    inline constexpr size_t MapSetLimit = 50;

    inline constexpr size_t ReTransmissionMapLimit = 2000;

    inline constexpr size_t LIMIT_SIZE_T = std::numeric_limits<size_t>::max();

    inline constexpr uint64_t LIMIT_UINT64_T = std::numeric_limits<uint64_t>::max();

    inline constexpr uint32_t LIMIT_UINT32_T = std::numeric_limits<uint32_t>::max();

    inline constexpr uint16_t LIMIT_UINT16_T = std::numeric_limits<uint16_t>::max();

    inline constexpr uint8_t LIMIT_UINT8_T = std::numeric_limits<uint8_t>::max();

    inline constexpr uint64_t ELICIT_OFFSET = LIMIT_UINT64_T - 1;

    inline constexpr uint16_t BLOCK_DEFAULT = LIMIT_UINT16_T;

    inline constexpr uint16_t NO_UNIMPORTANT_BLOCK = LIMIT_UINT16_T - 1;

    /*a is latter received, b is former received*/
    template <typename T>
    typename std::enable_if<std::is_unsigned<T>::value, bool>::type
    is_newer(T a, T b) {
        using SignedT = typename std::make_signed<T>::type;
        return static_cast<SignedT>(a - b) > 0;
    }


    class DynamicBitset {
    private:
        static constexpr size_t BITS_PER_BLOCK = 64;
        std::vector<uint64_t> data;
        size_t num_bits;

        size_t block_index(size_t pos) const { return pos / BITS_PER_BLOCK; }
        size_t bit_offset(size_t pos) const { return pos % BITS_PER_BLOCK; }

        void ensure_capacity(size_t new_bits) {
            size_t required_blocks = (new_bits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
            if (data.size() < required_blocks) {
                data.resize(required_blocks, 0);
            }
        }

    public:
        explicit DynamicBitset(size_t size = 0) : num_bits(size) {
            data.reserve(8192);
            ensure_capacity(size);
        }

        void set(size_t pos, bool value = true) {
            if (pos >= num_bits) {
                std::cerr << "pos:" << pos << ", num_bits:" << num_bits << std::endl;
                throw std::out_of_range("Set bit index out of range");
            }
            size_t block = block_index(pos), offset = bit_offset(pos);
            if (value) data[block] |= (1ULL << offset);
            else data[block] &= ~(1ULL << offset);
        }

        void reset(size_t pos) { set(pos, false); }

        void flip(size_t pos) {
            if (pos >= num_bits) {
                std::cerr << "pos:" << pos << ", num_bits:" << num_bits << std::endl;
                throw std::out_of_range("flip bit index out of range");
            }
            size_t block = block_index(pos), offset = bit_offset(pos);
            data[block] ^= (1ULL << offset);
        }

        bool test(size_t pos) const {
            if (pos >= num_bits) {
                std::cerr << "pos:" << pos << ", num_bits:" << num_bits << std::endl;
                throw std::out_of_range("test bit index out of range");
            }
            size_t block = block_index(pos), offset = bit_offset(pos);
            return (data[block] & (1ULL << offset)) != 0;
        }

        size_t count() const {
            size_t sum = 0;
            for (uint64_t block : data) sum += __builtin_popcountll(block);
            return sum;
        }

        size_t size() const { return num_bits; }

        void resize(size_t new_size) {
            ensure_capacity(new_size);
            if (new_size > num_bits) {
                size_t old_block = block_index(num_bits);
                size_t new_block = block_index(new_size);
                if (old_block != new_block) std::fill(data.begin() + old_block + 1, data.begin() + new_block + 1, 0);
                size_t old_offset = bit_offset(num_bits);
                if (old_offset != 0) data[old_block] &= (1ULL << old_offset) - 1;
            } else if (new_size < num_bits) {
                size_t new_last_block = block_index(new_size);
                size_t new_last_offset = bit_offset(new_size);
                if (new_last_offset != 0) {
                    data[new_last_block] &= (1ULL << new_last_offset) - 1;
                }
            }
            num_bits = new_size;
        }

        void shrink_to_fit() {
            size_t required_blocks = (num_bits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
            if (data.size() > required_blocks) {
                data.resize(required_blocks);
            }
        }

        std::string to_string() const {
            std::string result;
            for (size_t i = num_bits; i > 0; --i) result += test(i - 1) ? '1' : '0';
            return result;
        }

        class BitReference {
            friend class DynamicBitset;
            DynamicBitset& bitset;
            size_t index;
            BitReference(DynamicBitset& b, size_t i) : bitset(b), index(i) {}

        public:
            operator bool() const { return bitset.test(index); }
            BitReference& operator=(bool value) { bitset.set(index, value); return *this; }
            BitReference& operator=(const BitReference& other) { return *this = bool(other); }
            void flip() { bitset.flip(index); }
        };

        bool operator[](size_t index) const { return test(index); }
        BitReference operator[](size_t index) {
            if (index >= num_bits) {
                std::cerr << "index:" << index << ", num_bits:" << num_bits << std::endl;
                throw std::out_of_range("[] Bit index out of range");
            }
            return BitReference(*this, index);
        }

        void clear() {
            std::fill(data.begin(), data.end(), 0);
        }
    };

    /*Send timestamp queue*/
    class TSCircularQueue{
    private:
        /*first packet number + timestamp, last packet number + timestamp*/
        using DataType = std::pair<std::pair<uint64_t, std::chrono::system_clock::time_point>,
                                std::pair<uint64_t, std::chrono::system_clock::time_point>>;
        using TimeStamp = std::chrono::system_clock::time_point;
        std::vector<DataType> buffer; 
        std::vector<uint8_t> loss_status; // 0: not loss, 1: loss
        size_t head;                  
        size_t tail;                 
        size_t capacity;              
        size_t count;                 

    public:
        explicit TSCircularQueue(size_t capacity = 1024)
            : buffer(capacity), loss_status(capacity), head(0), tail(0), capacity(capacity), count(0) {}

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

        bool checklast(uint64_t first, uint64_t last) const {
            if (isEmpty()) {
                return false;
            }
            for (auto i = 0; i < size(); i++) {
                const auto& item = at(i);
                if (item.second.first == last && (item.second.first - item.first.first) >= 30) {
                    return true;
                }
            }
            
            return false;

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
                return std::nullopt;
            }

            TimeStamp result;

            bool has = false;
            while (size() > 0) {
                const auto& item = front();  
                if (item.second.first < value){
                    dequeue();
                }

                if (item.first.first <= value && item.second.first >= value){
                    if (item.first.first == item.second.first){
                        result = item.first.second;
                    }else{
                        result = item.first.second + (item.second.second - item.first.second) * (value - item.first.first) / (item.second.first - item.first.first) ; 
                    }
                    has = true;
                    break;
                }

                if (value < item.first.first){
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
                return RCset_body[index] == 1;
            }

            void insert(Offset_len offset_){
                auto index = get_index(offset_);
                if (index >= RCset_body.size()){
                    throw std::underflow_error("[RCset]: insert index beyond capacity_");
                }
                RCset_body[index] = 1;
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

    inline void log_print(void* src_, size_t len_, size_t print_len = LIMIT_SIZE_T) {
        if (!src_) {
            std::cerr << "Null pointer passed to log_print!" << std::endl;
            return;
        }
        
        auto* data = static_cast<uint8_t*>(src_);  
        
        if (print_len == LIMIT_SIZE_T){
            for (size_t i = 0; i < len_; i++) {
                std::cout << static_cast<int>(data[i]) << " ";  
            }
            std::cout << std::endl;
        }else {
            auto tmp_len = std::min(len_, print_len);
            for (size_t i = 0; i < tmp_len; i++) {
                std::cout << static_cast<int>(data[i]) << " ";  
            }
            std::cout << std::endl;
        }
    }


    inline void ip_print (struct sockaddr_storage & peeraddr, bool port_ = false) {
        char ipstr[INET6_ADDRSTRLEN] = {0};
        uint16_t port = 0;

        if (peeraddr.ss_family == AF_INET) {
            // IPv4
            const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&peeraddr);
            inet_ntop(AF_INET, &addr4->sin_addr, ipstr, sizeof(ipstr));
            if (port_){
                std::cout << "Peer IP: " << ipstr << ":" << ntohs(addr4->sin_port) << ", ";
            } else {
                std::cout << "Peer IP: " << ipstr << ", ";
            }   
        } else if (peeraddr.ss_family == AF_INET6) {
            // IPv6
            const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&peeraddr);
            inet_ntop(AF_INET6, &addr6->sin6_addr, ipstr, sizeof(ipstr));
            if (port_) {
                std::cout << "Peer IP: [" << ipstr << "]:" << ntohs(addr6->sin6_port) << ", ";
            } else {
                std::cout << "Peer IP: [" << ipstr << "]:" << ", ";
            }
        } else {
            return;
        }
    }
    
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

    /*In receive side, it's used to record msghdr index*/
    template<typename T, size_t Capacity>
    class SPSCQueue {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

        std::vector<T> buffer;
        size_t head = 0;
        size_t tail = 0;

    public:
        SPSCQueue() : buffer(Capacity = 330000) {}

        bool enqueue(const T& item) {
            size_t next_tail = (tail + 1) & (Capacity - 1);
            if (next_tail == __atomic_load_n(&head, __ATOMIC_ACQUIRE)) {
                return false; // full
            }
            buffer[tail] = item;
            __atomic_thread_fence(__ATOMIC_RELEASE);
            tail = next_tail;
            return true;
        }

        std::optional<T> dequeue() {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            size_t cur_tail = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
            if (head == cur_tail) {
                return std::nullopt; // empty
            }

            __builtin_prefetch(&buffer[(head + 1) & (Capacity - 1)], 0, 1);

            T item = buffer[head];
            head = (head + 1) & (Capacity - 1);
            return item;
        }
    };

    template<typename T, size_t Capacity>
    class FIFOQueue {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

        alignas(64) size_t head_ = 0;
        alignas(64) size_t tail_ = 0;
        std::vector<T> buffer_;

    public:
        FIFOQueue() : buffer_(Capacity) {}

        bool enqueue(const T& item) {
            size_t next_tail = (tail_ + 1) & (Capacity - 1);
            if (next_tail == head_) return false;
            buffer_[tail_] = item;
            tail_ = next_tail;
            return true;
        }

        std::optional<T> dequeue() {
            if (head_ == tail_) std::nullopt;
            T out = buffer_[head_];
            head_ = (head_ + 1) & (Capacity - 1);
            return out;
        }

        size_t enqueue_batch(const T* data, size_t max_count) {
            size_t space = (head_ - tail_ - 1) & (Capacity - 1);
            size_t count = std::min(max_count, space);

            size_t first_chunk = std::min(count, Capacity - tail_);
            for (size_t i = 0; i < first_chunk; ++i) {
                buffer_[tail_ + i] = data[i];
            }
            size_t second_chunk = count - first_chunk;
            for (size_t i = 0; i < second_chunk; ++i) {
                buffer_[i] = data[first_chunk + i];
            }

            tail_ = (tail_ + count) & (Capacity - 1);
            return count;
        }

        size_t dequeue_batch(T* out, size_t max_count) {
            size_t available = size();
            size_t count = std::min(max_count, available);

            size_t first_chunk = std::min(count, Capacity - head_);
            for (size_t i = 0; i < first_chunk; ++i) {
                out[i] = buffer_[head_ + i];
            }
            size_t second_chunk = count - first_chunk;
            for (size_t i = 0; i < second_chunk; ++i) {
                out[first_chunk + i] = buffer_[i];
            }

            head_ = (head_ + count) & (Capacity - 1);
            return count;
        }

        size_t dequeue_batch(std::vector<T>& out, size_t max_count) {
            size_t available = size();
            size_t count = std::min(max_count, available);

            size_t first_chunk = std::min(count, Capacity - head_);
            out.insert(out.end(), buffer_.begin() + head_, buffer_.begin() + head_ + first_chunk);

            size_t second_chunk = count - first_chunk;
            if (second_chunk > 0) {
                out.insert(out.end(), buffer_.begin(), buffer_.begin() + second_chunk);
            }

            head_ = (head_ + count) & (Capacity - 1);
            return count;
        }

        inline size_t size() const {
            return (tail_ - head_) & (Capacity - 1);
        }

        inline bool empty() const {
            return head_ == tail_;
        }

        void clear() {
            head_ = tail_ = 0;
        }
    };


    class PacketMapRingBuffer {
    public:
        explicit PacketMapRingBuffer(size_t capacity = MAP_CONST, uint64_t start_packet_number = 0)
            : capacity_(capacity),
            buffer_(capacity),
            head_(0),
            tail_(0),
            head_packet_number_(start_packet_number) {}

        ~PacketMapRingBuffer(){};

        /*
        1. important packet (whatever first transmission or retransmission)
        2. unimportant packet first transmissino
        3. unimportant packet retransmission
        */

        bool push(uint64_t offset_, Difference_len difference_,  Type ty_, uint64_t packet_number, uint8_t channel_ = 0) {
            size_t next_tail = (tail_ + 1) % capacity_;
            if (next_tail == head_) {
                std::cout<<"PacketMapRingBuffer full"<<std::endl;
                return false; 
            }
            

            buffer_[tail_].offset = offset_;
            buffer_[tail_].difference = difference_;
            buffer_[tail_].pkt_ty = ty_;
            buffer_[tail_].channel = channel_;
            tail_ = next_tail;
            // std::cout << "Packet number " << packet_number << ", head packet number " << head_packet_number_ << ", "<<size()<<", "<<buffer_.size()
            //     << ", "<<head_<<", "<<tail_<< std::endl;
            if (packet_number != (head_packet_number_ + size() - 1)) {
                _Exit(0);  
            }
            return true;
        }

        bool getOffset(uint64_t packet_number, uint64_t& out_offset, Difference_len & difference_) {
            uint64_t current_size = size();
            uint64_t tail_packet_number = head_packet_number_ + current_size;

            if (packet_number < head_packet_number_ || packet_number >= tail_packet_number) {
                return false;
            }

            if (packet_number > head_packet_number_) {
                size_t advance = packet_number - head_packet_number_;
                head_ = (head_ + advance) % capacity_;
                head_packet_number_ = packet_number;
            }

            size_t index = (head_ + (packet_number - head_packet_number_)) % capacity_;
            out_offset = buffer_[index].offset;
            return true;
        }

        bool tryFindDeletedOffset(uint64_t packet_number, uint64_t& out_offset, Difference_len& difference_) const {
            uint64_t buffer_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
            uint64_t buffer_end = head_packet_number_ + size();  

            if (packet_number < buffer_begin || packet_number >= buffer_end) {
                return false;  
            }

            size_t index = (head_ + (packet_number - head_packet_number_)) % capacity_;
            out_offset = buffer_[index].offset;
            difference_ = buffer_[index].difference;
            return true;
        }

        bool findOffsetAuto(uint64_t packet_number, uint64_t& out_offset, Difference_len& difference_) {
            if (getOffset(packet_number, out_offset, difference_)) {
                return true;
            }
            return tryFindDeletedOffset(packet_number, out_offset, difference_);
        }

        std::optional<std::pair<uint64_t, Difference_len>> findOffsetAuto(uint64_t packet_number) {
            uint64_t offset;
            Difference_len diff;
            if (findOffsetAuto(packet_number, offset, diff)) {
                return std::make_pair(offset, diff);
            }
            return std::nullopt;
        }

        template <typename Func>
        bool forEachSlotAutoRange(uint64_t start_packet, uint64_t end_packet, Func&& func) const {
            if (start_packet >= end_packet) return false;

            
            uint64_t full_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
            uint64_t full_end = head_packet_number_ + size();

            if (start_packet < full_begin || end_packet > full_end) {
                return false;  
            }

            size_t base_index = (head_ + (start_packet - head_packet_number_)) % capacity_;
            size_t index = base_index;

            for (uint64_t pkt = start_packet; pkt < end_packet; ++pkt) {
                func(pkt, buffer_[index]);
                index = (index + 1) % capacity_;
            }

            return true;
        }

        template <typename Func>
        bool forEachSlotAutoRangePartial(uint64_t start_packet, uint64_t end_packet, Func&& func) {
            if (start_packet >= end_packet) return false;

            uint64_t full_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
            uint64_t full_end = head_packet_number_ + size();

            uint64_t actual_start = std::max(start_packet, full_begin);
            uint64_t actual_end = std::min(end_packet, full_end);

            if (actual_start >= actual_end) return false;

            bool within_active_range = (actual_start >= head_packet_number_) && (actual_end <= head_packet_number_ + size());
            size_t index = (head_ + (actual_start - head_packet_number_)) % capacity_;

            if (within_active_range && actual_start > head_packet_number_) {
                size_t advance = actual_start - head_packet_number_;
                head_ = (head_ + advance) % capacity_;
                head_packet_number_ = actual_start;
            }

            for (uint64_t pkt = actual_start; pkt < actual_end; ++pkt) {
                bool delayed = false;
                if (pkt < head_packet_number_) {
                    delayed = true;
                }
                func(pkt, buffer_[index], delayed);
                if (within_active_range) {
                    head_ = (head_ + 1) % capacity_;
                    ++head_packet_number_;
                }
                index = (index + 1) % capacity_;
            }

            return true;
        }


        bool pop() {
            if (empty()) return false;
            head_ = (head_ + 1) % capacity_;
            ++head_packet_number_;
            return true;
        }

        std::vector<std::pair<uint64_t, uint64_t>> getAllMappings() const {
            std::vector<std::pair<uint64_t, uint64_t>> mappings;
            mappings.reserve(size());

            size_t idx = head_;
            uint64_t pkt = head_packet_number_;
            while (idx != tail_) {
                mappings.emplace_back(pkt, buffer_[idx].offset);
                idx = (idx + 1) % capacity_;
                ++pkt;
            }

            return mappings;
        }

        bool empty() const {
            return head_ == tail_;
        }

        size_t size() const {
            if (tail_ >= head_) return tail_ - head_;
            return capacity_ - head_ + tail_;
        }

        size_t capacity() const {
            return capacity_ - 1; 
        }

        size_t freeSlots() const {
            return capacity() - size();
        }

        uint64_t packetNumberBegin() const {
            return head_packet_number_;
        }

        uint64_t packetNumberEnd() const {
            return head_packet_number_ + size();
        }

    private:
        struct Slot {
            uint64_t offset; /* Control message: offset = std::numeric_limits<uint64_t>::max() - 1 */
            uint32_t difference;
            Type pkt_ty; /* Elicit or Application */
            uint8_t channel;
            uint8_t pad[18];
        };

        size_t capacity_;
        std::vector<Slot> buffer_;
        size_t head_;
        size_t tail_;
        uint64_t head_packet_number_;
    };
    


    struct BitPos {
        int64_t last_one = -1;
        int64_t last_zero = -1;
    };

    struct NextZeroInfo {
        int64_t first = -1;
        bool has_next = false;
        int64_t second = -1;
    };


    // 简易自定义 span
    template <typename T>
    class Span {
    public:
        Span() : data_(nullptr), size_(0) {}
        Span(const T* data, size_t size) : data_(data), size_(size) {}

        template <typename U, std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>, int> = 0>
        Span(const Span<U>& other) : data_(other.data()), size_(other.size()) {}

        const T* data() const { return data_; }
        size_t size() const { return size_; }
        const T& operator[](size_t idx) const {
            assert(idx < size_);
            return data_[idx];
        }
        bool empty() { return size_ == 0; }
    private:
        const T* data_;
        size_t size_;
    };

    inline BitPos find_last_1_and_0_avx2(Span<const uint64_t> bits, size_t num_bits,
                                     size_t offset_start, size_t offset_end) {
        BitPos result;
        size_t start_word = offset_start / 64;
        size_t end_word = offset_end / 64;

        for (int64_t i = static_cast<int64_t>(end_word); i >= static_cast<int64_t>(start_word); --i) {
            uint64_t w = bits[i];
            uint64_t mask = ~0ULL;

            if (i == static_cast<int64_t>(start_word)) {
                mask &= (~0ULL << (offset_start % 64));
            }
            if (i == static_cast<int64_t>(end_word)) {
                mask &= (1ULL << ((offset_end % 64) + 1)) - 1;
            }

            w &= mask;
            uint64_t w_inv = (~w) & mask;

            if (w && result.last_one == -1) {
                result.last_one = i * 64 + (63 - __builtin_clzll(w));
            }
            if (w_inv && result.last_zero == -1) {
                result.last_zero = i * 64 + (63 - __builtin_clzll(w_inv));
            }

            if (result.last_one != -1 && result.last_zero != -1)
                break;
        }

        return result;
    }

    inline int64_t find_next_bit_64(const uint64_t* bits, size_t num_bits,
                                    size_t offset_start, size_t offset_end,
                                    bool find_one) {
        // Placeholder for 64-bit scalar search
        return -1; // This function is not implemented yet
    }

    inline int64_t find_next_bit_native(const uint64_t* bits, size_t num_bits,
                                    size_t offset_start, size_t offset_end,
                                    bool find_one) {
        if (offset_start > offset_end || offset_end >= num_bits) return -1;

        for (size_t bit = offset_start; bit <= offset_end; ++bit) {
            size_t word_idx = bit / 64;
            size_t bit_idx = bit % 64;
            bool val = (bits[word_idx] >> bit_idx) & 1ULL;
            if (val == find_one) return bit;
        }
        return -1;
    }

    BitPos find_last_1_and_0_impl(Span<const uint64_t> bits, size_t num_bits,
                                size_t offset_start, size_t offset_end);

    // int64_t find_next_bit_avx512(Span<const uint64_t> bits, size_t num_bits,
    //                             size_t offset_start, size_t offset_end, bool find_one);

    class BitmapSpan {
    public:
        BitmapSpan();
        BitmapSpan(Span<const uint64_t> data, size_t start_bit, size_t end_bit);

        BitPos find_last_1_and_0() const;

        void reset_span(Span<const uint64_t> new_bits, size_t new_start_bit, size_t new_end_bit);

        bool empty();

        int64_t next_one_avx512();
        int64_t next_zero_avx512();

        void reset_next_indices();

    private:
        Span<const uint64_t> bits_;
        size_t start_bit_;
        size_t end_bit_;
        size_t num_bits_;
        size_t next_one_index_;
        size_t next_zero_index_;
        int64_t last_found_one_ = -1;
        int64_t last_found_zero_ = -1;
    };
}