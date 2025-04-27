#pragma once

#include <algorithm>
#include <stdlib.h>
#include <numeric>
#include <cstddef>    // size_t
#include <cstdint>    // uint64_t
#include <vector>     // std::vector
#include <stdexcept>  // std::out_of_range
#include <iostream>   // std::cout, std::endl
#include <string>     // std::string
#include <algorithm> 

namespace dmludp{
    enum class MetaFlag : uint8_t {
        Initial = 1,       
        Retransmission,
        Complete  
    };

    class DynamicBitset {
    private:
        static constexpr size_t BITS_PER_BLOCK = 64;
        std::vector<uint64_t> data;
        size_t num_bits;

        size_t block_index(size_t pos) const { return pos / BITS_PER_BLOCK; }
        size_t bit_offset(size_t pos) const { return pos % BITS_PER_BLOCK; }

        void ensure_capacity(size_t new_bits) {
            size_t required_blocks = (new_bits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
            // std::cout<<"1 ensure_capacity:"<<new_bits<<", "<<required_blocks<<std::endl;
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

    template <typename T>
        class CircularQueue {
        public:
            std::vector<T> data_;
            size_t head_;
            size_t tail_;
            size_t capacity_;

            CircularQueue(size_t capacity = 1000) 
                : data_(capacity), head_(0), tail_(0), capacity_(capacity)
            {}

            virtual void push_back(const T value) {
                data_[tail_] = value;
                tail_ = (tail_ + 1) % capacity_;
            }

            T pop_front() {
                if (empty()) {
                    throw std::runtime_error("Queue is empty, cannot remove element.");
                }
                T value = data_[head_];
                head_ = (head_ + 1) % capacity_;
                return value;
            }

            size_t size() const {
                if (tail_ >= head_) {
                    return tail_ - head_;
                } else {
                    return capacity_ - (head_ - tail_);
                }
            }

            bool empty() const {
                return head_ == tail_;
            }

            bool full() const {
                return ((tail_ + 1) % capacity_) == head_;
            }

            virtual void clear() {
                head_ = tail_ = 0;
            }

            virtual ~CircularQueue() = default; 
        };

    class SendBufferCircularQueue : public CircularQueue<uint64_t> {
        public:
            ssize_t last_value;

            SendBufferCircularQueue(size_t capacity = 10000) 
                : CircularQueue<uint64_t>(capacity), last_value(-1) 
            {}

            void push_back(uint64_t value) override {
                data_[tail_] = value;
                tail_ = (tail_ + 1) % capacity_;
                last_value = static_cast<ssize_t>(value); 
            }

            void clear() override {
                head_ = tail_ = 0;
                last_value = -1;
            }

            ssize_t get_last_value() const {
                return last_value;
            }

            ~SendBufferCircularQueue(){};
    };

 
    class SendBuf{
        public:
        /* SendMetaBuf*/
        void* meta_ptr;

        // ssize_t meta_ptr_len;
        size_t meta_ptr_len;

        void* meta_ptr2;

        // ssize_t meta_ptr2_len; 
        size_t meta_ptr2_len;

        uint64_t meta_len;

        ssize_t meta_left;

        size_t meta_sent = 0;;

        ssize_t meta_pos;

        std::vector<uint64_t> meta_len2;

        SendBufferCircularQueue rcq;

        DynamicBitset bits_set;

        MetaFlag meta_status = MetaFlag::Initial;

        size_t send_buffer_size;

        size_t ack_count = 0;

        SendBuf(size_t packet_len): 
        send_buffer_size(packet_len)
        {
            meta_len2.resize(2);
        };

        ~SendBuf(){};

        bool is_empty(){
            if (rcq.empty() && meta_left <= 0) return true;
            return false;
        }

        size_t sentComplete(){
            return meta_sent;
        }

        void add_Meta(struct iovec* iovecs, int iovecs_len){
            meta_status = MetaFlag::Initial;
            meta_sent = 0;

            meta_ptr = nullptr;
            meta_ptr_len = 0;
            meta_ptr2 = nullptr;
            meta_ptr2_len = 0; 
            meta_left = 0;
            meta_len = 0;
         
            for (auto i = 0; i < iovecs_len; i++){
                meta_left += iovecs[i].iov_len;
                meta_sent += iovecs[i].iov_len;
                meta_len += (iovecs[i].iov_len + send_buffer_size - 1)/send_buffer_size;
                meta_len2[i] = iovecs[i].iov_len;
                if (i == 0){
                    meta_ptr = iovecs[i].iov_base;
                    meta_ptr_len = iovecs[i].iov_len;
                }else{
                    meta_ptr2 = iovecs[i].iov_base;
                    meta_ptr2_len = iovecs[i].iov_len;
                }
            }
            
            meta_pos = 0;
            if(meta_len != bits_set.size()){
                bits_set.resize(meta_len);
            }
            bits_set.clear();
            ack_count = 0;
            rcq.clear();
        }

        ssize_t off_front(){
            ssize_t off = -1;
            if(meta_status == MetaFlag::Initial){
                if (meta_left > 0){
                    if (meta_pos == 0){
                        off = meta_pos * send_buffer_size;
                        meta_left -= 48;
                    }else{
                        off = (meta_pos - 1) * send_buffer_size + 48;
                        meta_left -= send_buffer_size;
                    }
                    meta_pos++;
                    if (meta_left <= 0){
                        meta_left = 0;
                        meta_status = MetaFlag::Retransmission;
                    }
                }
            }else if(meta_status == MetaFlag::Retransmission){
                if (!rcq.empty()){
                    off = rcq.pop_front();
                }
            }
            return off;
        }


        void acknowledege_and_drop(uint64_t in_offset, bool is_drop){
            if (is_drop){
                /*bits_set.set(buffer_offset_convertor(in_offset));*/
                auto index = 0;
                if (in_offset >= 48){
                    index = (in_offset - 48) / send_buffer_size + 1;
                }else{
                    index = in_offset / send_buffer_size;
                }
                if (bits_set[index] == 0){
                    // std::cout<<"in_offset:"<<in_offset<<std::endl;
                    bits_set.set(index);
                    ack_count++;
                }
            }else{
                rcq.push_back(in_offset);
            }
            // std::cout<<"acknowledege_and_drop:"<<in_offset<<", count_:"<<ack_count<<std::endl;
            if (ack_count == bits_set.size()){
                meta_status = MetaFlag::Complete;
            }
        } 

        void ack_check(){
            std::cout << "ack_count:" << ack_count << ", " << bits_set.size() << ", " << bits_set.count() << std::endl;
        }


        bool emit(struct iovec& out, ssize_t& out_len, uint64_t& out_off){
            bool stop = false;
            
            out_len = 0;
            auto tmp_off = off_front();

            std::cout<<"emit:"<<(meta_ptr_len+meta_ptr2_len)<<std::endl;

            if (tmp_off == -1){
                out_len = -1;
                stop = true;
                return stop;
            }
            out_off = tmp_off;
            if (out_off == 0){
                out_len = meta_ptr_len;
                out.iov_base = meta_ptr;
                out.iov_len = meta_ptr_len;
            }else{
                out_len = std::min(send_buffer_size, size_t(meta_ptr2_len - (out_off - 48)));
                out.iov_base = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(meta_ptr2) + out_off - 48);
                // out.iov_base = (void *)(meta_ptr2 + out_off - 48);
                out.iov_len = out_len;
            }      

            return stop;
        }

        bool written_complete(){
            return meta_status == MetaFlag::Complete;
        }


        void clear(){
            meta_pos = -1;
            meta_sent = 0;
            meta_len = 0;
            meta_left = 0;
            for(auto &e :meta_len2){
                e = 0;
            }
        };

        size_t get_status(){
            return static_cast<size_t>(meta_status);
        }

        void to_json() const{
            std::cout << "{";
            std::cout << "\"class\": \"SendBuf\", ";
            std::cout << "\"meta_left\": \"" << meta_left << "\", ";
            std::cout << "\"meta_sent\": \"" << meta_sent << "\"";
            if (meta_len == 48){
                std::cout << "\"meta_element 0\": \"" << (void*)meta_ptr << ", " << meta_ptr_len<< "\"";
            }else{
                std::cout << "\"meta_element 0\": \"" << (void*)meta_ptr << ", " << meta_ptr_len<< "\"";
                std::cout << "\"meta_element 1\": "<<(void*)meta_ptr2 << ", " << meta_ptr2_len<< "\"";
            }
            std::cout << "\"bits_set\": \"" << bits_set.size() << "\"";
            std::cout << "}";
        }
    };
    
}
