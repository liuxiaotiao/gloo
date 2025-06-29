#pragma once
#include <chrono>
#include <iostream>
#include <optional>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <omp.h>
#include <thread>
#include <execution>
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

    inline constexpr size_t DataBlock = 16;

    inline constexpr size_t MapSetLimit = 50;

    inline constexpr size_t ReTransmissionMapLimit = 2000;

    inline constexpr size_t LIMIT_SIZE_T = std::numeric_limits<size_t>::max();

    inline constexpr uint64_t LIMIT_UINT64_T = std::numeric_limits<uint64_t>::max();

    inline constexpr uint32_t LIMIT_UINT32_T = std::numeric_limits<uint32_t>::max();

    inline constexpr uint16_t LIMIT_UINT16_T = std::numeric_limits<uint16_t>::max();

    inline constexpr uint8_t LIMIT_UINT8_T = std::numeric_limits<uint8_t>::max();

    inline constexpr uint64_t ELICIT_OFFSET = LIMIT_UINT64_T - 1;

    inline constexpr uint16_t BLOCK_DEFAULT = LIMIT_UINT16_T;

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
            data.reserve(5200);
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
                // std::cout<<"1 new_size:"<<new_size<<", "<<num_bits<<std::endl;
                size_t old_block = block_index(num_bits);
                size_t new_block = block_index(new_size);
                if (old_block != new_block) std::fill(data.begin() + old_block + 1, data.begin() + new_block + 1, 0);
                size_t old_offset = bit_offset(num_bits);
                if (old_offset != 0) data[old_block] &= (1ULL << old_offset) - 1;
            } else if (new_size < num_bits) {
                // std::cout<<"2 new_size:"<<new_size<<", "<<num_bits<<std::endl;
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
        size_t head;                  
        size_t tail;                 
        size_t capacity;              
        size_t count;                 

    public:
        explicit TSCircularQueue(size_t capacity = 1024)
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
                // return std::nullopt;
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

    // class PacketMapRingBuffer {
    // public:
    //     explicit PacketMapRingBuffer(size_t capacity, uint64_t start_packet_number = 0)
    //         : capacity_(capacity),
    //         buffer_(capacity),
    //         head_(0),
    //         tail_(0),
    //         head_packet_number_(start_packet_number) {}

    //     ~PacketMapRingBuffer(){};

    //     bool push(uint64_t offset_, Difference_len difference_, Priority_len priority_ = 0) {
    //         size_t next_tail = (tail_ + 1) % capacity_;
    //         if (next_tail == head_) {
    //             std::cout<<"PacketMapRingBuffer full"<<std::endl;
    //             return false; 
    //         }

    //         buffer_[tail_].offset = offset_;
    //         buffer_[tail_].difference = difference_;
    //         // buffer_[tail_].priority = priority_;
    //         tail_ = next_tail;
    //         return true;
    //     }

    //     bool getOffset(uint64_t packet_number, uint64_t& out_offset, Difference_len & difference_) {
    //         uint64_t current_size = size();
    //         uint64_t tail_packet_number = head_packet_number_ + current_size;

    //         if (packet_number < head_packet_number_ || packet_number >= tail_packet_number) {
    //             return false;
    //         }

    //         if (packet_number > head_packet_number_) {
    //             size_t advance = packet_number - head_packet_number_;
    //             head_ = (head_ + advance) % capacity_;
    //             head_packet_number_ = packet_number;
    //         }

    //         size_t index = (head_ + (packet_number - head_packet_number_)) % capacity_;
    //         out_offset = buffer_[index].offset;
    //         return true;
    //     }

    //     bool tryFindDeletedOffset(uint64_t packet_number, uint64_t& out_offset, Difference_len& difference_) const {
    //         // 考虑最早还能访问到的 packet_number 是：
    //         uint64_t buffer_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
    //         uint64_t buffer_end = head_packet_number_ + size();  // 当前最大 packet_number（非包含）

    //         if (packet_number < buffer_begin || packet_number >= buffer_end) {
    //             return false;  // 被覆盖了
    //         }

    //         size_t index = (head_ + (packet_number - head_packet_number_)) % capacity_;
    //         out_offset = buffer_[index].offset;
    //         difference_ = buffer_[index].difference;
    //         return true;
    //     }

    //     bool findOffsetAuto(uint64_t packet_number, uint64_t& out_offset, Difference_len& difference_) {
    //         if (getOffset(packet_number, out_offset, difference_)) {
    //             return true;
    //         }
    //         return tryFindDeletedOffset(packet_number, out_offset, difference_);
    //     }

    //     std::optional<std::pair<uint64_t, Difference_len>> findOffsetAuto(uint64_t packet_number) {
    //         uint64_t offset;
    //         Difference_len diff;
    //         if (findOffsetAuto(packet_number, offset, diff)) {
    //             return std::make_pair(offset, diff);
    //         }
    //         return std::nullopt;
    //     }

    //     template <typename Func>
    //     bool forEachSlotAutoRange(uint64_t start_packet, uint64_t end_packet, Func&& func) const {
    //         if (start_packet >= end_packet) return false;

            
    //         uint64_t full_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
    //         uint64_t full_end = head_packet_number_ + size();

    //         if (start_packet < full_begin || end_packet > full_end) {
    //             return false;  
    //         }

    //         size_t base_index = (head_ + (start_packet - head_packet_number_)) % capacity_;
    //         size_t index = base_index;

    //         for (uint64_t pkt = start_packet; pkt < end_packet; ++pkt) {
    //             func(pkt, buffer_[index]);
    //             index = (index + 1) % capacity_;
    //         }

    //         return true;
    //     }

    //     template <typename Func>
    //     bool forEachSlotAutoRangePartial(uint64_t start_packet, uint64_t end_packet, Func&& func) {
    //         if (start_packet >= end_packet) return false;

    //         uint64_t full_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
    //         uint64_t full_end = head_packet_number_ + size();

    //         uint64_t actual_start = std::max(start_packet, full_begin);
    //         uint64_t actual_end = std::min(end_packet, full_end);

    //         if (actual_start >= actual_end) return false;

    //         bool within_active_range = (actual_start >= head_packet_number_) && (actual_end <= head_packet_number_ + size());
    //         size_t index = (head_ + (actual_start - head_packet_number_)) % capacity_;

    //         if (within_active_range && actual_start > head_packet_number_) {
    //             size_t advance = actual_start - head_packet_number_;
    //             head_ = (head_ + advance) % capacity_;
    //             head_packet_number_ = actual_start;
    //         }

    //         // for (uint64_t pkt = actual_start; pkt < actual_end; ++pkt) {
    //         //     func(pkt, buffer_[index]);
    //         //     if (within_active_range) {
    //         //         // std::cout<<"map:"<<pkt<<", "<<buffer_[index].offset<<", "<<buffer_[index].difference<<std::endl;
    //         //         head_ = (head_ + 1) % capacity_;
    //         //         ++head_packet_number_;
    //         //         // std::cout<<"head_packet_number_:"<<head_packet_number_<<std::endl;
    //         //     }
    //         //     index = (index + 1) % capacity_;
    //         // }

    //         for (uint64_t pkt = actual_start; pkt < actual_end; ++pkt) {
    //             bool delayed = false;
    //             if (pkt < head_packet_number_) {
    //                 delayed = true;
    //             }
    //             func(pkt, buffer_[index], delayed);
    //             if (within_active_range) {
    //                 // std::cout<<"map:"<<pkt<<", "<<buffer_[index].offset<<", "<<buffer_[index].difference<<std::endl;
    //                 head_ = (head_ + 1) % capacity_;
    //                 ++head_packet_number_;
    //                 // std::cout<<"head_packet_number_:"<<head_packet_number_<<std::endl;
    //             }
    //             index = (index + 1) % capacity_;
    //         }

    //         return true;
    //     }


    //     bool pop() {
    //         if (empty()) return false;
    //         head_ = (head_ + 1) % capacity_;
    //         ++head_packet_number_;
    //         return true;
    //     }

    //     std::vector<std::pair<uint64_t, uint64_t>> getAllMappings() const {
    //         std::vector<std::pair<uint64_t, uint64_t>> mappings;
    //         mappings.reserve(size());

    //         size_t idx = head_;
    //         uint64_t pkt = head_packet_number_;
    //         while (idx != tail_) {
    //             mappings.emplace_back(pkt, buffer_[idx].offset);
    //             idx = (idx + 1) % capacity_;
    //             ++pkt;
    //         }

    //         return mappings;
    //     }

    //     bool empty() const {
    //         return head_ == tail_;
    //     }

    //     size_t size() const {
    //         if (tail_ >= head_) return tail_ - head_;
    //         return capacity_ - head_ + tail_;
    //     }

    //     size_t capacity() const {
    //         return capacity_ - 1; 
    //     }

    //     size_t freeSlots() const {
    //         return capacity() - size();
    //     }

    //     uint64_t packetNumberBegin() const {
    //         return head_packet_number_;
    //     }

    //     uint64_t packetNumberEnd() const {
    //         return head_packet_number_ + size();
    //     }

    // private:
    //     struct Slot {
    //         uint64_t offset;
    //         uint32_t difference;
    //         uint16_t len;
    //         uint16_t priority;
    //     };

    //     size_t capacity_;
    //     std::vector<Slot> buffer_;
    //     size_t head_;
    //     size_t tail_;
    //     uint64_t head_packet_number_;
    // };


    class PacketMapRingBuffer {
    public:
        explicit PacketMapRingBuffer(size_t capacity, uint64_t start_packet_number = 0)
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

        bool push(uint64_t offset_, Difference_len difference_, Packet_status_len packet_status_, Type_len ty_ = type::Application) {
            size_t next_tail = (tail_ + 1) % capacity_;
            if (next_tail == head_) {
                std::cout<<"PacketMapRingBuffer full"<<std::endl;
                return false; 
            }

            buffer_[tail_].offset = offset_;
            buffer_[tail_].difference = difference_;
            buffer_[tail_].pkt_status = packet_status_;
            buffer_[tail_].type = ty_;
            tail_ = next_tail;
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
            uint8_t pkt_ty; /* Elicit or Application */
            uint8_t blockinfo; /* contain: 0 / 1 */
            uint8_t channel; /* Different cwnd */
            uint8_t pkt_status; /* Unimportant packet will be drop when it marked as retransmission status */
        };

        size_t capacity_;
        std::vector<Slot> buffer_;
        size_t head_;
        size_t tail_;
        uint64_t head_packet_number_;
    };


    
    // class BitmapSpan {
    // public:
    //     // 默认构造
    //     BitmapSpan() = default;

    //     // 区间构造
    //     BitmapSpan(std::span<const uint64_t> span, size_t start, size_t end) {
    //         reset(span, start, end);
    //     }

    //     // 重设span与区间
    //     void reset(std::span<const uint64_t> span, size_t start, size_t end) {
    //         data_ = span;
    //         start_ = start;
    //         end_ = end;
    //         curr_one_index_ = start_;
    //         curr_zero_index_ = start_;
    //         assert_valid();
    //     }

    //     // 查找区间最低位1和0，返回（最低1全局位置，最低0全局位置），没有则-1
    //     std::pair<int64_t, int64_t> lowest_one_zero_pos() const {
    //         assert_valid();
    //         int64_t lowest_one = -1, lowest_zero = -1;
    //         size_t start_word = start_ / 64;
    //         size_t start_off  = start_ % 64;
    //         size_t end_word   = (end_ - 1) / 64;
    //         size_t end_off    = (end_ - 1) % 64;

    //         // 单word区间
    //         if (start_word == end_word) {
    //             uint64_t mask = ((1ULL << (end_off - start_off + 1)) - 1) << start_off;
    //             uint64_t v1 = data_[start_word] & mask;
    //             uint64_t v0 = (~data_[start_word]) & mask;
    //             if (v1 != 0) lowest_one = start_word * 64 + __builtin_ctzll(v1);
    //             if (v0 != 0) lowest_zero = start_word * 64 + __builtin_ctzll(v0);
    //             return {lowest_one, lowest_zero};
    //         }

    //         // 头word
    //         uint64_t head_mask = ~0ULL << start_off;
    //         uint64_t v1 = data_[start_word] & head_mask;
    //         uint64_t v0 = (~data_[start_word]) & head_mask;
    //         if (v1 != 0 && lowest_one == -1) lowest_one = start_word * 64 + __builtin_ctzll(v1);
    //         if (v0 != 0 && lowest_zero == -1) lowest_zero = start_word * 64 + __builtin_ctzll(v0);

    //         // 中间word
    //         for (size_t i = start_word + 1; i < end_word; ++i) {
    //             if (lowest_one == -1 && data_[i] != 0)
    //                 lowest_one = i * 64 + __builtin_ctzll(data_[i]);
    //             if (lowest_zero == -1 && ~data_[i] != 0)
    //                 lowest_zero = i * 64 + __builtin_ctzll(~data_[i]);
    //             if (lowest_one != -1 && lowest_zero != -1) break;
    //         }

    //         // 尾word
    //         uint64_t tail_mask = (1ULL << (end_off + 1)) - 1;
    //         v1 = data_[end_word] & tail_mask;
    //         v0 = (~data_[end_word]) & tail_mask;
    //         if (v1 != 0 && lowest_one == -1) lowest_one = end_word * 64 + __builtin_ctzll(v1);
    //         if (v0 != 0 && lowest_zero == -1) lowest_zero = end_word * 64 + __builtin_ctzll(v0);

    //         return {lowest_one, lowest_zero};
    //     }

    //     // 从index起查下一个1（全局位置，不存在-1）
    //     int64_t next_one(size_t index) const {
    //         if (index < start_ || index >= end_) return -1;
    //         size_t word = index / 64;
    //         size_t bit_in_word = index % 64;
    //         size_t end_word = (end_ - 1) / 64;
    //         size_t end_off = (end_ - 1) % 64;

    //         uint64_t tail_mask = ~0ULL;
    //         if (word == end_word) tail_mask = (1ULL << (end_off + 1)) - 1;
    //         uint64_t mask = tail_mask & (~0ULL << bit_in_word);
    //         uint64_t v = data_[word] & mask;
    //         if (v != 0) return word * 64 + __builtin_ctzll(v);

    //         for (size_t i = word + 1; i <= end_word; ++i) {
    //             uint64_t word_mask = (i == end_word) ? ((1ULL << (end_off + 1)) - 1) : ~0ULL;
    //             v = data_[i] & word_mask;
    //             if (v != 0) return i * 64 + __builtin_ctzll(v);
    //         }
    //         return -1;
    //     }

    //     // 从index起查下一个0（全局位置，不存在-1）
    //     int64_t next_zero(size_t index) const {
    //         if (index < start_ || index >= end_) return -1;
    //         size_t word = index / 64;
    //         size_t bit_in_word = index % 64;
    //         size_t end_word = (end_ - 1) / 64;
    //         size_t end_off = (end_ - 1) % 64;

    //         uint64_t tail_mask = ~0ULL;
    //         if (word == end_word) tail_mask = (1ULL << (end_off + 1)) - 1;
    //         uint64_t mask = tail_mask & (~0ULL << bit_in_word);
    //         uint64_t v = (~data_[word]) & mask;
    //         if (v != 0) return word * 64 + __builtin_ctzll(v);

    //         for (size_t i = word + 1; i <= end_word; ++i) {
    //             uint64_t word_mask = (i == end_word) ? ((1ULL << (end_off + 1)) - 1) : ~0ULL;
    //             v = (~data_[i]) & word_mask;
    //             if (v != 0) return i * 64 + __builtin_ctzll(v);
    //         }
    //         return -1;
    //     }

    //     // 自动推进查找游标，查下一个1
    //     int64_t next_one_index() {
    //         if (curr_one_index_ >= end_) return -1;
    //         int64_t idx = next_one(curr_one_index_);
    //         curr_one_index_ = (idx == -1 ? end_ : idx + 1);
    //         return idx;
    //     }

    //     // 自动推进查找游标，查下一个0
    //     int64_t next_zero_index() {
    //         if (curr_zero_index_ >= end_) return -1;
    //         int64_t idx = next_zero(curr_zero_index_);
    //         curr_zero_index_ = (idx == -1 ? end_ : idx + 1);
    //         return idx;
    //     }

    //     // 手动设置查找游标
    //     void set_one_index(size_t idx) { curr_one_index_ = idx; }
    //     void set_zero_index(size_t idx) { curr_zero_index_ = idx; }

    //     // 区间大小（bit数）
    //     size_t bit_size() const { return end_ > start_ ? end_ - start_ : 0; }

    //     // 全局起始和结束
    //     size_t start() const { return start_; }
    //     size_t end() const { return end_; }

    // private:
    //     std::span<const uint64_t> data_{};
    //     size_t start_ = 0, end_ = 0;
    //     size_t curr_one_index_ = 0, curr_zero_index_ = 0;

    //     void assert_valid() const {
    // #ifndef NDEBUG
    //         assert(data_.data() != nullptr && end_ > start_);
    //         assert(data_.size() * 64 >= end_);
    // #endif
    //     }
    // };


    struct BitPos {
        int64_t last_one = -1;
        int64_t last_zero = -1;
    };

    struct NextZeroInfo {
        int64_t first = -1;
        bool has_next = false;
        int64_t second = -1;
    };

    // ===============================
    // 查找最后 1 和 0（标量高效版）
    // ===============================
    inline BitPos find_last_1_and_0_impl(std::span<const uint64_t> bits, size_t num_bits,
                                        size_t offset_start, size_t offset_end) {
        BitPos result;
        size_t start_word = offset_start / 64;
        size_t end_word = offset_end / 64;
        size_t start_offset = offset_start % 64;
        size_t end_offset = offset_end % 64;

        for (int64_t i = static_cast<int64_t>(end_word); i >= static_cast<int64_t>(start_word); --i) {
            uint64_t word = bits[i];
            uint64_t mask = ~0ULL;
            if (i == static_cast<int64_t>(start_word))
                mask &= (~0ULL << start_offset);
            if (i == static_cast<int64_t>(end_word))
                mask &= (1ULL << (end_offset + 1)) - 1;

            size_t bits_from = i * 64;
            if (bits_from + 64 > num_bits) {
                size_t valid_bits = num_bits - bits_from;
                mask &= (valid_bits >= 64) ? ~0ULL : ((1ULL << valid_bits) - 1);
            }

            uint64_t masked = word & mask;
            uint64_t masked_inv = (~word) & mask;

            if (masked)
                result.last_one = std::max(result.last_one,
                                        static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked))));
            if (masked_inv)
                result.last_zero = std::max(result.last_zero,
                                            static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked_inv))));
        }
        return result;
    }

    // ===============================
    // AVX-512 查找下一个 bit（1 或 0）
    // ===============================
    inline int64_t find_next_bit_avx512(std::span<const uint64_t> bits, size_t num_bits,
                                        size_t offset_start, size_t offset_end, bool find_one) {
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(bits.data());
        size_t byte_start = offset_start / 8;
        size_t byte_end = offset_end / 8;

        for (size_t i = byte_start; i + 63 <= byte_end; i += 64) {
            __m512i v = _mm512_loadu_si512(reinterpret_cast<const void*>(byte_ptr + i));
            if (!find_one)
                v = _mm512_xor_si512(v, _mm512_set1_epi8(-1));

            __mmask64 mask = _mm512_cmpneq_epi8_mask(v, _mm512_setzero_si512());
            if (mask != 0) {
                int bit_pos = __builtin_ctzll(mask);
                return static_cast<int64_t>((i * 8) + bit_pos);
            }
        }

        // fallback scalar
        size_t bit_tail = std::max(byte_start, byte_end - 63) * 8;
        for (size_t b = bit_tail; b <= offset_end; ++b) {
            if (b < offset_start) continue;
            size_t word_idx = b / 64;
            size_t bit_idx = b % 64;
            bool bit = (bits[word_idx] >> bit_idx) & 1ULL;
            if (bit == find_one)
                return static_cast<int64_t>(b);
        }
        return -1;
    }

    // ===============================
    // BitmapSpan 类
    // ===============================
    class BitmapSpan {
    public:
        BitmapSpan()
        : bits_(), start_bit_(0), end_bit_(0), num_bits_(0),
          next_one_index_(0), next_zero_index_(0) {}

        BitmapSpan(std::span<const uint64_t> data, size_t start_bit, size_t end_bit)
            : bits_(data), start_bit_(start_bit), end_bit_(end_bit),
            num_bits_(end_bit >= start_bit ? end_bit - start_bit + 1 : 0),
            next_one_index_(start_bit), next_zero_index_(start_bit) {
            assert(start_bit <= end_bit);
        }

        BitPos find_last_1_and_0() const {
            BitPos pos = find_last_1_and_0_impl(bits_, num_bits_, 0, end_bit_ - start_bit_);
            if (pos.last_one != -1) pos.last_one += start_bit_;
            if (pos.last_zero != -1) pos.last_zero += start_bit_;
            return pos;
        }

        void reset_span(std::span<const uint64_t> new_bits, size_t new_start_bit, size_t new_end_bit) {
            assert(new_start_bit <= new_end_bit);
            bits_ = new_bits;
            start_bit_ = new_start_bit;
            end_bit_ = new_end_bit;
            num_bits_ = end_bit_ - start_bit_ + 1;
            next_one_index_ = start_bit_;
            next_zero_index_ = start_bit_;
        }

        int64_t next_one_avx512() {
            if (next_one_index_ > end_bit_) return -1;
            int64_t pos = find_next_bit_avx512(bits_, num_bits_,
                                            next_one_index_ - start_bit_,
                                            end_bit_ - start_bit_, true);
            if (pos != -1) {
                pos += start_bit_;
                next_one_index_ = pos + 1;
                return pos;
            }
            next_one_index_ = end_bit_ + 1;
            return -1;
        }

        int64_t next_zero_avx512() {
            if (next_zero_index_ > end_bit_) return -1;
            int64_t pos = find_next_bit_avx512(bits_, num_bits_,
                                            next_zero_index_ - start_bit_,
                                            end_bit_ - start_bit_, false);
            if (pos != -1) {
                pos += start_bit_;
                next_zero_index_ = pos + 1;
                return pos;
            }
            next_zero_index_ = end_bit_ + 1;
            return -1;
        }

        void reset_next_indices() {
            next_one_index_ = start_bit_;
            next_zero_index_ = start_bit_;
        }

    private:
        std::span<const uint64_t> bits_;
        size_t start_bit_;
        size_t end_bit_;
        size_t num_bits_;
        size_t next_one_index_;
        size_t next_zero_index_;
    };
}