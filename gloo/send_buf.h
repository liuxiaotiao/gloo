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
    enum class MetaFlag : uint8_t {
        Initial = 1,       
        Retransmission,
        Complete  
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
        /* SendMetaBuf*/
        void* meta_ptr = nullptr;

        // ssize_t meta_ptr_len;
        size_t meta_ptr_len;

        void* meta_ptr2 = nullptr;

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

        bool acknowldge_status = true;

        uint64_t lastpacketOffset = std::numeric_limits<uint64_t>::max();

        uint64_t lastlossOffset = 0;

        size_t initlosscount = 0;

        public:

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
                    // std::cout<<"meta_ptr2:"<<meta_ptr2<<std::endl;
                    meta_ptr2_len = iovecs[i].iov_len;
                }
            }
            
            meta_pos = 0;
            if(meta_len != bits_set.size()){
                bits_set.resize(meta_len);
                // retranmission_map.resize(meta_len);
            }
            bits_set.clear();
            // retranmission_map.clear();
            ack_count = 0;
            rcq.clear();
            acknowldge_status = true;
            lastlossOffset = 0;
            initlosscount = 0;
            // if (iovecs_len == 1) {
            //     lastpacketOffset = 0;
            // } else {
            //     lastpacketOffset = 48 + (meta_ptr2_len / 1440)* 1440;
            // }
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
                        lastpacketOffset = off;
                        meta_status = MetaFlag::Retransmission;
                    }
                }
            }else if(meta_status == MetaFlag::Retransmission){
                if (!rcq.empty()){
                    off = rcq.pop_front();
                    /////////////
                    auto index = 0;
                    // std::cout<<"off:"<<off<<", "<<meta_len;
                    if (off != 0){
                        index = round_up((off - 48), 1440) + 1;
                    }
                    // std::cout<<", "<<index<<std::endl;
                    
                    if (bits_set[index] == 1)
                    {
                        off = -1;
                    }
                    ///////////////
                }
            }
            return off;
        }

        // void acknowledege_and_drop(uint64_t in_offset, bool is_drop){
        //     /* 6.6
        //     In fact, we didn't do extra operation for received packet, we just need to modify the timeout packet and delay ack packet
        //     */
        //     if (is_drop){
        //         /*bits_set.set(buffer_offset_convertor(in_offset));*/
        //         auto index = 0;
        //         if (in_offset >= 48){
        //             index = (in_offset - 48) / send_buffer_size + 1;
        //         }else{
        //             index = in_offset / send_buffer_size;
        //         }
        //         if (bits_set[index] == 0){
        //             // std::cout<<"in_offset:"<<in_offset<<std::endl;
        //             bits_set.set(index);
        //             ack_count++;
        //         }
        //     }else{
        //         /*
        //         NO pop front cause duplicate packet sent again and again.
        //         */
        //         ////////////////
        //         auto index = 0;
        //         if (in_offset >= 48){
        //             index = (in_offset - 48) / send_buffer_size + 1;
        //         }else{
        //             index = in_offset / send_buffer_size;
        //         }
        //         if (bits_set[index] == 0){
        //             // std::cout<<"in_offset:"<<in_offset<<std::endl;
        //             rcq.push_back(in_offset);
        //         }
        //         // rcq.push_back(in_offset);
        //         ///////////////
        //     }
        //     // std::cout<<"acknowledege_and_drop:"<<in_offset<<", count_:"<<ack_count<<std::endl;
        //     if (ack_count == bits_set.size()){
        //         meta_status = MetaFlag::Complete;
        //     }
        // } 

        void acknowledege_and_drop(uint64_t in_offset, bool is_drop){
            if (is_drop){
                if (acknowldge_status) {
                    if (in_offset == lastpacketOffset) {
                        ack_count = bits_set.size() - initlosscount;
                        acknowldge_status = false;
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
                        ack_count++;
                    }
                }
            }else{
                if (acknowldge_status){
                    if (!rcq.empty()){
                        if (in_offset > rcq.back()) {
                            auto index = 0;
                            if (in_offset >= 48){
                                index = (in_offset - 48) / send_buffer_size + 1;
                            }else{
                                index = in_offset / send_buffer_size;
                            }
                            if (bits_set[index] == 0){
                                rcq.push_back(in_offset);
                                if (in_offset > lastlossOffset) {
                                    ++initlosscount;
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
                            rcq.push_back(in_offset);
                            if (in_offset > lastlossOffset) {
                                ++initlosscount;
                            }
                        }
                    }

                    if (in_offset == lastpacketOffset) {
                        ack_count = bits_set.size() - initlosscount;
                        acknowldge_status = false;
                    }
                }else {
                    auto index = 0;
                    if (in_offset >= 48){
                        index = (in_offset - 48) / send_buffer_size + 1;
                    }else{
                        index = in_offset / send_buffer_size;
                    }
                    if (bits_set[index] == 0){
                        rcq.push_back(in_offset);
                    }
                }
                
            }
            
            // std::cout<<"acknowledege_and_drop:"<<in_offset<<", count_:"<<ack_count<<std::endl;
            if (ack_count == bits_set.size()){
                meta_status = MetaFlag::Complete;
            }
        }


        /*
        The retransmission message are decided by first MaxpacketOffset
        Future improvement: reduce rcq real push_back, using virtual_head and virtual_tail to control message
        */
        // void acknowledege_and_drop(uint64_t in_offset, bool is_drop){
        //     if (is_drop){
        //         if (acknowldge_status) {
        //             if (in_offset == lastpacketOffset) {
        //                 ack_count = bits_set.size() - rcq.size();
        //             }
        //         } else {
        //             auto index = 0;
        //             if (in_offset >= 48){
        //                 index = (in_offset - 48) / send_buffer_size + 1;
        //             }else{
        //                 index = in_offset / send_buffer_size;
        //             }
        //             if (bits_set[index] == 0){
        //                 bits_set.set(index);
        //                 ack_count++;
        //             }
        //         }
        //     }else{
        //         if (acknowldge_status) {
        //              auto index = 0;
        //             if (in_offset >= 48){
        //                 index = (in_offset - 48) / send_buffer_size + 1;
        //             }else{
        //                 index = in_offset / send_buffer_size;
        //             }
        //             if (bits_set[index] == 0){
        //                 rcq.push_back(in_offset);
        //             }
        //         } 

        //         if (acknowldge_status && in_offset == lastpacketOffset) {
        //             ack_count = bits_set.size() - rcq.size();
        //         }
        //     }
            
        //     // std::cout<<"acknowledege_and_drop:"<<in_offset<<", count_:"<<ack_count<<std::endl;
        //     if (ack_count == bits_set.size()){
        //         meta_status = MetaFlag::Complete;
        //     }
        // }

        void RetranmissionQueueClear() {

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


        bool emit(struct iovec& out, ssize_t& out_len, uint64_t& out_off){
            bool stop = false;
            
            out_len = 0;
            auto tmp_off = off_front();

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
