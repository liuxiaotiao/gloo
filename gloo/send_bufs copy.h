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
#include "tool.h"

namespace dmludp{
    enum class Channel : uint8_t {
        Unimportant = 0,     
        Important = 1,  
    };

    enum class MetaFlag : uint8_t {
        /* Important channel status*/
        Initial = 1,       
        Retransmission,
        Complete,
        /* Unimportant channel status*/
        Initial_unimportance,
        Retransmission_unimportance,
        Complete_unimportance, /* Just transmission complete*/
        Ack_complete_unimportance /* Real unimportant packet transmission complete, receiver know unimportant channel info*/
    };


    class BitmapSpan {
        public:
            // 可以通过vector或span构造
            BitmapSpan(std::span<const uint64_t> span) : data_(span) {}
            BitmapSpan(const std::vector<uint64_t>& vec) : data_(vec.data(), vec.size()) {}

            // 找最低位1
            int64_t lowest_one_pos() const {
                size_t n = data_.size();
                for (size_t i = 0; i < n; ++i) {
                    if (data_[i] != 0) {
                        return i * 64 + __builtin_ctzll(data_[i]);
                    }
                }
                return -1; // 全0
            }

            // 找最低位0
            int64_t lowest_zero_pos() const {
                size_t n = data_.size();
                for (size_t i = 0; i < n; ++i) {
                    if (~data_[i] != 0) {
                        return i * 64 + __builtin_ctzll(~data_[i]);
                    }
                }
                return -1; // 全1
            }

            // 返回 (最低1的全局位置，最低0的全局位置)；没有则为-1
            std::pair<int64_t, int64_t> lowest_one_zero_pos() const {
                int64_t lowest_one = -1, lowest_zero = -1;
                size_t start_word = start_ / 64;
                size_t start_off  = start_ % 64;
                size_t end_word   = (end_ - 1) / 64;
                size_t end_off    = (end_ - 1) % 64;

                // 单word区间
                if (start_word == end_word) {
                    uint64_t mask = ((1ULL << (end_off - start_off + 1)) - 1) << start_off;
                    uint64_t v1 = data_[start_word] & mask;
                    uint64_t v0 = (~data_[start_word]) & mask;
                    if (v1 != 0) lowest_one = start_word * 64 + __builtin_ctzll(v1);
                    if (v0 != 0) lowest_zero = start_word * 64 + __builtin_ctzll(v0);
                    return {lowest_one, lowest_zero};
                }

                // 头word
                uint64_t head_mask = ~0ULL << start_off;
                uint64_t v1 = data_[start_word] & head_mask;
                uint64_t v0 = (~data_[start_word]) & head_mask;
                if (v1 != 0 && lowest_one == -1) lowest_one = start_word * 64 + __builtin_ctzll(v1);
                if (v0 != 0 && lowest_zero == -1) lowest_zero = start_word * 64 + __builtin_ctzll(v0);

                // 中间word
                for (size_t i = start_word + 1; i < end_word; ++i) {
                    if (lowest_one == -1 && data_[i] != 0)
                        lowest_one = i * 64 + __builtin_ctzll(data_[i]);
                    if (lowest_zero == -1 && ~data_[i] != 0)
                        lowest_zero = i * 64 + __builtin_ctzll(~data_[i]);
                    if (lowest_one != -1 && lowest_zero != -1) break; // 都找到了
                }

                // 尾word
                uint64_t tail_mask = (1ULL << (end_off + 1)) - 1;
                v1 = data_[end_word] & tail_mask;
                v0 = (~data_[end_word]) & tail_mask;
                if (v1 != 0 && lowest_one == -1) lowest_one = end_word * 64 + __builtin_ctzll(v1);
                if (v0 != 0 && lowest_zero == -1) lowest_zero = end_word * 64 + __builtin_ctzll(v0);

                return {lowest_one, lowest_zero};
            }


            // 可选：获取 span 长度
            size_t size() const { return data_.size() * 64; }

        private:
            std::span<const uint64_t> data_;
        };

    class bitmap {


        inline int lowest_one_pos(uint64_t x) {
            if (x == 0) return -1;
            return __builtin_ctzll(x); // gcc/clang内建，返回最低位1的位置
        }

        // 找最低位0（最右边的0）的位置，如果x==~0ULL则返回-1
        inline int lowest_zero_pos(uint64_t x) {
            if (~x == 0) return -1;
            return __builtin_ctzll(~x); // 取反后再找最低位1
        }

        int64_t find_next_zero(const std::vector<uint64_t>& bitmap, size_t index) {
            size_t total_bits = bitmap.size() * 64;
            if (index + 1 >= total_bits) return -1;

            size_t word_idx = (index + 1) / 64;
            size_t bit_in_word = (index + 1) % 64;

            // 当前word: 从bit_in_word开始找0
            uint64_t word = bitmap[word_idx] | ((1ULL << bit_in_word) - 1);
            if (~word != 0) {
                int offset = __builtin_ctzll(~word);
                return word_idx * 64 + offset;
            }

            // 后续word: 只要有0，必然在~bitmap[word_idx]有1
            for (size_t i = word_idx + 1; i < bitmap.size(); ++i) {
                if (~bitmap[i] != 0) {
                    int offset = __builtin_ctzll(~bitmap[i]);
                    return i * 64 + offset;
                }
            }

            // 全部都是1，没有0
            return -1;
        }

        // 返回下一个1的位置，未找到返回-1
        int64_t find_next_one(const std::vector<uint64_t>& bitmap, size_t index) {
            size_t total_bits = bitmap.size() * 64;
            if (index + 1 >= total_bits) return -1;

            size_t word_idx = (index + 1) / 64;
            size_t bit_in_word = (index + 1) % 64;

            // 当前word: 从bit_in_word开始找1
            uint64_t word = bitmap[word_idx] & (~((1ULL << bit_in_word) - 1));
            if (word != 0) {
                int offset = __builtin_ctzll(word);
                return word_idx * 64 + offset;
            }

            // 后续word: 只要有1
            for (size_t i = word_idx + 1; i < bitmap.size(); ++i) {
                if (bitmap[i] != 0) {
                    int offset = __builtin_ctzll(bitmap[i]);
                    return i * 64 + offset;
                }
            }

            // 全部都是0，没有1
            return -1;
        }
    }

    template <typename T>
        class CircularQueue {
        public:
            std::vector<T> data_;
            size_t head_;
            size_t tail_;
            size_t capacity_;

            CircularQueue(size_t capacity = 1024) 
                : data_(capacity), head_(0), tail_(0), capacity_(capacity)
            {}

            virtual void push_back(const T value) {
                if (full()) {
                    throw std::runtime_error("Queue is full, cannot add element.");
                }
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

         
            const T& front() const {
                if (empty()) {
                    throw std::runtime_error("Queue is empty, cannot access front element.");
                }
                return data_[head_];
            }

            const T& back() const {
                if (empty()) {
                    throw std::runtime_error("Queue is empty, cannot access back element.");
                }
                size_t last_index = (tail_ + capacity_ - 1) % capacity_;
                return data_[last_index];
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

            SendBufferCircularQueue(size_t capacity = 65536) 
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
        private:
        /* SendMetaBuf */
        void* meta_ptr = nullptr;

        size_t meta_ptr_len;

        void* meta_ptr2 = nullptr;

        size_t meta_ptr2_len;

        uint64_t meta_len;

        ssize_t meta_left;

        size_t meta_sent = 0;;

        ssize_t meta_pos;

        /* Important queue info */
        SendBufferCircularQueue rcq_important;

        MetaFlag meta_status_important = MetaFlag::Initial;

        size_t ack_count_important = 0;

        size_t packet_count_important = 0;

        bool acknowldge_status_important = true;

        /* Unimportant queue info */
        SendBufferCircularQueue rcq_unimportant;

        MetaFlag meta_status_unimportant = MetaFlag::Initial_unimportance;

        size_t ack_count_unimportant = 0;

        size_t ack_count_unimportant = 0;

        size_t packet_count_unimportant = 0;

        bool acknowldge_status_unimportant = true;
        
        /* Record all retransmission data received info */
        DynamicBitset bits_set;

        /* Packet maximum payload */
        size_t send_buffer_size;

        public:
        /* Important part */
        uint64_t lastpacketOffset_important = std::numeric_limits<uint64_t>::max();

        uint64_t lastlossOffset_important = 0;

        size_t initlosscount_important = 0;

        uint64_t lossreord_important = std::numeric_limits<uint64_t>::max();

        /* Unimportant part */
        uint64_t lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();

        uint64_t lastlossOffset_unimportant = 0;

        size_t initlosscount_unimportant = 0;

        uint64_t lossreord_unimportant = std::numeric_limits<uint64_t>::max();

        SendBuf(size_t packet_len): 
        send_buffer_size(packet_len){};

        ~SendBuf(){};

        bool is_empty() {
            if (rcq_important.empty() && rcq_unimportant.empty() && meta_left <= 0 /* && special */) return true;
            return false;
        }

        size_t sentComplete(){
            return meta_sent;
        }

        void add_Meta(struct iovec* iovecs, int iovecs_len){
            // meta_status = MetaFlag::Initial;
            meta_sent = 0;

            /* Meta data */
            meta_ptr = nullptr;
            meta_ptr_len = 0;
            meta_ptr2 = nullptr;
            meta_ptr2_len = 0; 
            meta_left = 0;
            meta_len = 0;

            /* Store buffer info: pointer + length */
            for (auto i = 0; i < iovecs_len; i++){
                meta_left += iovecs[i].iov_len;
                meta_sent += iovecs[i].iov_len;
                meta_len += (iovecs[i].iov_len + send_buffer_size - 1)/send_buffer_size;
                if (i == 0){
                    meta_ptr = iovecs[i].iov_base;
                    meta_ptr_len = iovecs[i].iov_len;
                }else{
                    meta_ptr2 = iovecs[i].iov_base;
                    meta_ptr2_len = iovecs[i].iov_len;
                }
            }
            
            /* Received bitset reset */
            meta_pos = 0;
            if(meta_len != bits_set.size()){
                bits_set.resize(meta_len);
            }

            bits_set.clear();

            /* Importance init */
            rcq_important.clear();
            ack_count_important = 0;
            packet_count_important = 0; /* Important packet count in this block */
            acknowldge_status_important = true;
            meta_status_important = MetaFlag::Initial;

            // lastlossOffset_important = 0;
            initlosscount_important = 0;
            lossreord_important = std::numeric_limits<uint64_t>::max();

            if (iovecs_len != 1) {
                /*
                1. Only important: importantIndex(!0), unimportantIndex(-1)
                2. Only unimportant: importantIndex(-1), unimportantIndex(!0)
                3. Both important and unimportat: importantIndex(!0), unimportantIndex(!0)
                */
                auto importantIndex = bitmap.last_one();
                auto unimportantIndex = bitmap.last_zero();

                if (importantIndex != -1 && unimportantIndex == -1){
                    lastpacketOffset_important = 48 + (importantIndex + 1) * MAX_SEND_UDP_PAYLOAD_SIZE;

                    /* unimportant part */
                    rcq_unimportant.clear();
                    ack_count_unimportant = 0;
                    packet_count_unimportant = 0;
                    acknowldge_status_unimportant = false;
                    meta_status_unimportant = MetaFlag::Ack_complete_unimportance;

                    lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();
                    initlosscount_unimportant = 0;
                    lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                } else if (importantIndex == -1 && unimportantIndex != -1) {
                    lastpacketOffset_important = 0;

                    /* unimportant part */
                    rcq_unimportant.clear();
                    ack_count_unimportant = 0;
                    packet_count_unimportant = 0;
                    acknowldge_status_unimportant = true;
                    meta_status_unimportant = MetaFlag::Initial_unimportance;

                    lastpacketOffset_unimportant = 48 + (unimportantIndex + 1) * MAX_SEND_UDP_PAYLOAD_SIZE;
                    initlosscount_unimportant = 0;
                    lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                } else {
                    lastpacketOffset_important = 48 + (importantIndex + 1) * MAX_SEND_UDP_PAYLOAD_SIZE;

                    /* unimportant part */
                    rcq_unimportant.clear();
                    ack_count_unimportant = 0;
                    packet_count_unimportant = 0;
                    acknowldge_status_unimportant = true;
                    meta_status_unimportant = MetaFlag::Initial_unimportance;

                    lastpacketOffset_unimportant = 48 + (unimportantIndex + 1) * MAX_SEND_UDP_PAYLOAD_SIZE;
                    initlosscount_unimportant = 0;
                    lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                }
            } else {
                /* Just control message */
                lastpacketOffset_important = 0;

                /* Unimportant part setting */
                lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance; /* No unimportant packet need to transmission */
            }
        }

        // ssize_t off_front_important(){
        //     ssize_t off = -1;
        //     if(meta_status_important == MetaFlag::Initial){
        //         if (meta_left > 0){
        //             if (meta_pos == 0){
        //                 off = meta_pos * send_buffer_size;
        //                 meta_left -= 48;
        //                 packet_count_important++;
        //             }else{
        //                 auto index = bitmap.next_one();
        //                 if (index != -1) {
        //                     off = index * send_buffer_size + 48;
        //                     meta_left -= send_buffer_size;
        //                     packet_count_important++;
        //                 } else {  
        //                     meta_status_important = MetaFlag::Retransmission;
        //                 }  
        //             }
        //             meta_pos++;
        //             if (meta_left <= 0){
        //                 meta_left = 0;
        //                 meta_status_important = MetaFlag::Retransmission;
        //             }
        //         }
        //     }else if(meta_status_important == MetaFlag::Retransmission){
        //         if (!rcq_important.empty()){
        //             uint64_t tmp_off = rcq_important.pop_front();
        //             if (tmp_off == std::numeric_limits<uint64_t>::max() - 1){
        //                 off = -1;
        //             } else {
        //                 auto index = 0;
        //                 if (off != 0){
        //                     index = round_up((off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
        //                 }
                        
        //                 if (bits_set[index] == 1)
        //                 {
        //                     off = -1;
        //                 }
        //             }
                    
        //         }
        //     }

        //     if (off == -1 && meta_status_unimportant == MetaFlag::Complete_unimportance) {
        //         off = -2;
        //     }
            
        //     /* Add blocks + status received ack part */
        //     /*
        //     if meta_status_unimportant == MetaFlag::Complete_unimportance
        //         off = std::numeric_limits<uint64_t>::max() - 1;
        //         return off, special
        //     */
        //     return off;
        // }

        ssize_t off_front_important(){
            ssize_t off = -1;
            if(meta_status_important == MetaFlag::Initial){
                if (meta_left > 0){
                    if (meta_pos == 0){
                        off = meta_pos * send_buffer_size;
                        meta_left -= 48;
                        packet_count_important++;
                    }else{
                        auto index = bitmap.next_one();
                        if (index != -1) {
                            off = index * send_buffer_size + 48;
                            meta_left -= send_buffer_size;
                            packet_count_important++;
                        } else {  
                            meta_status_important = MetaFlag::Retransmission;
                        }  
                    }
                    meta_pos++;
                    if (meta_left <= 0){
                        meta_left = 0;
                        meta_status_important = MetaFlag::Retransmission;
                    }
                }
            }else if(meta_status_important == MetaFlag::Retransmission){
                while (!rcq_important.empty()) {
                    uint64_t tmp_off = rcq_important.pop_front();
                    if (tmp_off == std::numeric_limits<uint64_t>::max() - 1){
                        off = -1;
                    } else {
                        auto index = 0;
                        if (off != 0){
                            index = round_up((off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
                        }
                        
                        if (bits_set[index] == 1)
                        {
                            off = -1;
                        } else {
                            break;
                        }
                    }
                }
            } else if (meta_status_important == MetaFlag::Complete) {

            }

            if (off == -1 && meta_status_unimportant == MetaFlag::Complete_unimportance) {
                off = -2;
            }
            
            /* Add blocks + status received ack part */
            /*
            if meta_status_unimportant == MetaFlag::Complete_unimportance
                off = std::numeric_limits<uint64_t>::max() - 1;
                return off, special
            */
            return off;
        }

        ssize_t off_front_unimportant () {
            ssize_t off = -1;
            if (meta_status_unimportant == MetaFlag::Initial_unimportance){
                if (meta_left > 0){
                    auto index = bitmap.next_zero();
                    if (index != -1) {
                        off = index * send_buffer_size + 48;
                        meta_left -= send_buffer_size;
                        unimportant_status_ = MetaFlag::Initial_unimportance;
                        if (off == lastlossOffset_unimportant) {
                            meta_status_unimportant = MetaFlag::Retransmission_unimportance;
                        }
                    } else {
                        std::cout<<"off_front_unimportant error"<<std::endl;
                        _Exit(0);
                        meta_status_unimportant = MetaFlag::Retransmission_unimportance;
                    }
                }
            } else if(meta_status_unimportant == MetaFlag::Retransmission_unimportance){
                while (!rcq_unimportant.empty()) {
                    off = rcq_unimportant.pop_front();
                    auto index = 0;
                    if (off != 0){
                        index = round_up((off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
                    }

                    if (rcq_unimportant.empty() && lastlosspacket/* ack first transmission packet mark it as true*/){
                        lastlosspacket = false;
                        unimportant_status_ = MetaFlag::Complete_unimportance;
                    }

                    if (bits_set[index] == 1){
                        off = -1;
                    } else {
                        break;
                    }
                } 
            } else if (unimportant_status_ == MetaFlag::Complete_unimportance) {
                if () {

                }
            }
            return off;
        }

        // ssize_t off_front_unimportant(MetaFlag &unimportant_status_){
        //     ssize_t off = -1;
        //     if (meta_status_unimportant == MetaFlag::Initial_unimportance){
        //         if (meta_left > 0){
        //             auto index = bitmap.next_zero();
        //             if (index != -1) {
        //                 off = index * send_buffer_size + 48;
        //                 meta_left -= send_buffer_size;
        //                 unimportant_status_ = MetaFlag::Initial_unimportance;
        //                 if (off == lastlossOffset_unimportant) {
        //                     meta_status_unimportant = MetaFlag::Retransmission_unimportance;
        //                 }
        //             } else {
        //                 std::cout<<"off_front_unimportant error"<<std::endl;
        //                 _Exit(0);
        //                 meta_status_unimportant = MetaFlag::Retransmission_unimportance;
        //             }
        //         }
        //     } else if(meta_status_unimportant == MetaFlag::Retransmission_unimportance){
        //         while (!rcq_unimportant.empty()) {
        //             off = rcq_unimportant.pop_front();
        //             auto index = 0;
        //             if (off != 0){
        //                 index = round_up((off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
        //             }
                    
        //             if (bits_set[index] == 1){
        //                 off = -1;
        //             } else {
        //                 if (!rcq_unimportant.empty()){
        //                     unimportant_status_ = MetaFlag::Retransmission_unimportance;
        //                 } else {
        //                     // unimportant_status_ = MetaFlag::Complete_unimportance;
        //                 }
        //             }
        //         }
        //     } else {
        //         // unimportant_status_ = MetaFlag::Ack_complete_unimportance;
        //     }
        //     return off;
        // }

        void acknowledege_and_drop(uint64_t in_offset, bool is_drop, bool realack){
            if (is_drop){
                if (acknowldge_status_important) {
                    if (in_offset == lastpacketOffset_important) {
                        ack_count_important = packet_count_important - initlosscount_important;
                        acknowldge_status_important = false;
                    }
                } else {
                    auto index = 0;
                    if (in_offset >= 48){
                        index = (in_offset - 48) / send_buffer_size + 1;
                    }else{
                        index = in_offset / send_buffer_size;
                    }
                    if (bits_set[index] == 0){
                        bits_set.set(index);
                        ack_count_important++;
                    }
                }
            }else{
                if (acknowldge_status_important){
                    if (!rcq_important.empty()){
                        if (in_offset > rcq_important.back()) {
                            auto index = 0;
                            if (in_offset >= 48){
                                index = (in_offset - 48) / send_buffer_size + 1;
                            }else{
                                index = in_offset / send_buffer_size;
                            }
                  
                            if (bits_set[index] == 0){
                                rcq_important.push_back(in_offset);
                                if (lossreord_important == std::numeric_limits<uint64_t>::max()){
                                    lossreord_important = in_offset;
                                    ++initlosscount_important;
                                } else {
                                    if (in_offset > lossreord_important) {
                                        ++initlosscount_important;
                                        lossreord_important = in_offset;
                                    }
                                }
                                
                            }
                        }
                    }else {
                        auto index = 0;
                        if (in_offset >= 48){
                            index = (in_offset - 48) / send_buffer_size + 1;
                        }else{
                            index = in_offset / send_buffer_size;
                        }
             
                        if (bits_set[index] == 0){
                            rcq_important.push_back(in_offset);
                            if (lossreord_important == std::numeric_limits<uint64_t>::max()){
                                lossreord_important = in_offset;
                                ++initlosscount_important;
                            } else {
                                if (in_offset > lossreord_important) {
                                    ++initlosscount_important;
                                    lossreord_important = in_offset;
                                }
                            }
                            
                        }
                    }

                    if (in_offset == lastpacketOffset_important) {
                        ack_count_important = packet_count_important - initlosscount_important;
                        acknowldge_status_important = false;
                    }
                }else {
                    auto index = 0;
                    if (in_offset >= 48){
                        index = (in_offset - 48) / send_buffer_size + 1;
                    }else{
                        index = in_offset / send_buffer_size;
                    }
                    if (bits_set[index] == 0){
                        rcq_important.push_back(in_offset);
                    }
                }
                
            }
            
            if (ack_count_important == packet_count_important){
                meta_status_important = MetaFlag::Complete;
            }

            if (realack) {
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
            }
        }

        void acknowledege_and_drop_unimportant(uint64_t in_offset, bool is_drop, bool realack = false){
            if (realack) {
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
                return;
            }

            if (is_drop){
                if (acknowldge_status_unimportant) {
                    if (in_offset == lastpacketOffset_unimportant) {
                        // std::cout<<"in_offset:"<<in_offset<<", "<<lastpacketOffset<<std::endl;
                        ack_count_unimportant = packet_count_unimportant - initlosscount_unimportant;
                        acknowldge_status_unimportant = false;
                        
                    }
                } else {
                    auto index = 0;
                    if (in_offset >= 48){
                        index = (in_offset - 48) / send_buffer_size + 1;
                    }else{
                        index = in_offset / send_buffer_size;
                    }
                    if (bits_set[index] == 0){
                        bits_set.set(index);
                        ack_count_unimportant++;
                    }
                }
            }else{
                if (acknowldge_status_unimportant){
                    if (!rcq_unimportant.empty()){
                        if (in_offset > rcq_unimportant.back()) {
                            auto index = 0;
                            if (in_offset >= 48){
                                index = (in_offset - 48) / send_buffer_size + 1;
                            }else{
                                index = in_offset / send_buffer_size;
                            }
                  
                            if (bits_set[index] == 0){
                                rcq_unimportant.push_back(in_offset);
                                if (lossreord_unimportant == std::numeric_limits<uint64_t>::max()){
                                    lossreord_unimportant = in_offset;
                                    ++initlosscount_unimportant;
                                } else {
                                    if (in_offset > lossreord_unimportant) {
                                        ++initlosscount_unimportant;
                                        lossreord_unimportant = in_offset;
                                    }
                                }
                                
                            }
                        }
                    }else {
                        auto index = 0;
                        if (in_offset >= 48){
                            index = (in_offset - 48) / send_buffer_size + 1;
                        }else{
                            index = in_offset / send_buffer_size;
                        }
             
                        if (bits_set[index] == 0){
                            rcq_unimportant.push_back(in_offset);
                            if (lossreord_unimportant == std::numeric_limits<uint64_t>::max()){
                                lossreord_unimportant = in_offset;
                                ++initlosscount_unimportant;
                            } else {
                                if (in_offset > lossreord_unimportant) {
                                    ++initlosscount_unimportant;
                                    lossreord_unimportant = in_offset;
                                }
                            }
                            
                        }
                    }

                    if (in_offset == lastpacketOffset_unimportant) {
                        ack_count_unimportant = packet_count_unimportant - initlosscount_unimportant;
                        acknowldge_status_unimportant = false;
                    }
                }else {
                    auto index = 0;
                    if (in_offset >= 48){
                        index = (in_offset - 48) / send_buffer_size + 1;
                    }else{
                        index = in_offset / send_buffer_size;
                    }
                    if (bits_set[index] == 0){
                        rcq_unimportant.push_back(in_offset);
                    }
                }
                
            }
        }

        size_t round_up(uint64_t a, uint64_t b){
            if (b == 0) {
                throw std::invalid_argument("Division by zero is not allowed");
            }

            return (a + b - 1) / b;
        }

        void ack_check(){
            std::cout << "ack_count:" << ack_count << ", " << bits_set.size() << ", " << bits_set.count() <<", "<< rcq.size() << std::endl;
        }


        bool emit(struct iovec& out, ssize_t& out_len, uint64_t& out_off, uint16_t & blocks, uint8_t & status_){
            bool stop = false;
            
            out_len = 0;
            auto tmp_off = off_front_important();

            if (tmp_off == -1){
                out_len = -1;
                stop = true;
                return stop;
            }

            if (tmp_off > -1) {
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
            } else {
                /* Special */
                out_off = std::numeric_limits<uint64_t>::max() - 1;
                out_len = 0;
                out.iov_base = nullptr;
                out.iov_len = 0;
            }
                

            if (meta_status_important != MetaFlag::Initial) {
                blocks = packet_count_important;
            }

            if (meta_status_unimportant == MetaFlag::Complete_unimportance) {
                status_ = 1;
            }

            return stop;
        }


        bool emit_unimportance(struct iovec& out, ssize_t& out_len, uint64_t& out_off, uint16_t & blocks, uint8_t & status_){
            bool stop = false;
            
            out_len = 0;
            auto tmp_off = off_front_unimportant();

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
                out.iov_len = out_len;
            }      

            if (meta_status_important != MetaFlag::Initial) {
                blocks = packet_count_important;
            }

            if (meta_status_unimportant == MetaFlag::Complete_unimportance) {
                status_ = 1;
            }

            return stop;
        }

        bool written_complete(){
            return meta_status_important == MetaFlag::Complete /* MetaFlag::Complete also represents that receiver master blocks info*/
                && meta_status_unimportant == MetaFlag::Ack_complete_unimportance;
        }

        void clear(){
            meta_pos = -1;
            meta_sent = 0;
            meta_len = 0;
            meta_left = 0;
        };

        size_t get_status_importance(){
            return static_cast<size_t>(meta_status_important);
        }

        size_t get_status_unimportance(){
            return static_cast<size_t>(meta_status_unimportant);
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
