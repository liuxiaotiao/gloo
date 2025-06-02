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
    inline constexpr  size_t HEADER_LENGTH = sizeof(Header);

    // The default max_datagram_size used in congestion control.
    inline constexpr  size_t MAX_SEND_UDP_PAYLOAD_SIZE = 1440;

    inline constexpr  size_t MAX_ACK_UDP_PAYLOAD_SIZE = 1400;

    inline constexpr  size_t RX_CONST = 8192;

    inline constexpr  size_t ONCE_LIMIT = 1300;

    inline constexpr  size_t ONCE_SEND_LIMIT = ONCE_LIMIT;

    inline constexpr  size_t ONCE_RECEIVE_LIMINT = ONCE_LIMIT;

    inline constexpr  double alpha = 0.875;

    inline constexpr  double beta = 0.25;

    inline constexpr  size_t DataBlock = 16;

    inline constexpr  size_t MapSetLimit = 50;

    inline constexpr  size_t ReTransmissionMapLimit = 2000;

    inline constexpr  size_t LIMIT_SIZE_T = std::numeric_limits<size_t>::max();

    inline constexpr  uint64_t LIMIT_UINT64_T = std::numeric_limits<uint64_t>::max();

    inline constexpr  uint32_t LIMIT_UINT32_T = std::numeric_limits<uint32_t>::max();

    inline constexpr  uint16_t LIMIT_UINT16_T = std::numeric_limits<uint16_t>::max();

    inline constexpr  uint8_t LIMIT_UINT8_T = std::numeric_limits<uint8_t>::max();

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
                throw std::out_of_range("Bit index out of range");
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
            memset(offsets.data(),0, offsets.size());
            // std::fill(offsets.begin(), offsets.end(), 0);
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
                // expand_capacity();
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
        explicit PacketMapRingBuffer(size_t capacity, uint64_t start_packet_number = 0)
            : capacity_(capacity),
            buffer_(capacity),
            head_(0),
            tail_(0),
            head_packet_number_(start_packet_number) {}

        bool push(uint64_t offset, Difference_len difference_) {
            size_t next_tail = (tail_ + 1) % capacity_;
            if (next_tail == head_) {
                return false; 
            }

            buffer_[tail_].offset = offset;
            buffer_[tail_].difference = difference_;
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
            // 考虑最早还能访问到的 packet_number 是：
            uint64_t buffer_begin = (head_packet_number_ >= capacity_) ? (head_packet_number_ - capacity_ + 1) : 0;
            uint64_t buffer_end = head_packet_number_ + size();  // 当前最大 packet_number（非包含）

            if (packet_number < buffer_begin || packet_number >= buffer_end) {
                return false;  // 被覆盖了
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

            for (uint64_t pkt = actual_start; pkt < actual_end; ++pkt) {
                func(pkt, buffer_[index]);
                if (within_active_range && pkt == head_packet_number_) {
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
            uint64_t offset;
            uint32_t difference;
            uint16_t len_;
            uint16_t priority;
        };

        size_t capacity_;
        std::vector<Slot> buffer_;
        size_t head_;
        size_t tail_;
        uint64_t head_packet_number_;
    };
}