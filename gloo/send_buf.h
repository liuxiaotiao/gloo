#pragma once

#include <algorithm>
#include <stdlib.h>
#include <numeric>
#include <boost/dynamic_bitset.hpp>

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
        uint8_t* meta_ptr;
        
        uint64_t meta_len;

        ssize_t meta_left;

        ssize_t meta_pos;

        std::vector<std::pair<uint8_t*, ssize_t>> meta_element;

        std::vector<uint64_t> meta_len2;

        // CircularQueue rcq;
        SendBufferCircularQueue rcq;

        boost::dynamic_bitset<> bits_set;

        MetaFlag meta_status = MetaFlag::Initial;
        
        std::vector<uint64_t> retransmision_offset;

        ssize_t start_pos = 0;

        size_t send_buffer_size;

        size_t ack_count = 0;

        SendBuf(size_t packet_len): 
        send_buffer_size(packet_len), 
        retransmision_offset(10000, 0)
        {};

        ~SendBuf(){};

        bool is_empty(){
            if (rcq.empty() && meta_left <= 0) return true;
            return false;
        }

        void add_Meta(struct iovec* iovecs, int iovecs_len){
            meta_status = MetaFlag::Initial;

            for (auto i = 0; i < iovecs_len; i++){
                meta_element.push_back(std::make_pair(reinterpret_cast<uint8_t*>(iovecs[i].iov_base), iovecs[i].iov_len));
                meta_left += iovecs[i].iov_len;
                meta_len += (iovecs[i].iov_len + send_buffer_size - 1)/send_buffer_size;
                meta_len2.push_back(iovecs[i].iov_len);
            }
            meta_pos = -1;
            bits_set.resize(meta_len);
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


        void acknowledege_and_drop(uint32_t in_offset, bool is_drop){
            if (is_drop){
                /*bits_set.set(buffer_offset_convertor(in_offset));*/
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
            }else{
                rcq.push_back(in_offset);
            }
            if (ack_count == bits_set.size()){
                meta_status = MetaFlag::Complete;
            }
        } 


        bool emit(struct iovec& out, ssize_t& out_len, uint32_t& out_off){
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
                out_len = meta_element[0].second;
                out.iov_base = (void *)(meta_element[0].first + out_off);
                out.iov_len = out_len;

            }else{
                out_len = std::min(send_buffer_size, size_t(meta_len2[1] - (out_off - 48)));
                out.iov_base = (void *)(meta_element[1].first + out_off);
                out.iov_len = out_len;
            }      

            return stop;
        }

        bool written_complete(){
            return meta_status == MetaFlag::Complete;
        }


        void clear(){
            meta_pos = -1;
            for (auto i = 0; i < retransmision_offset.size(); i++){
                retransmision_offset[i] = 0;
            }
            bits_set.reset();
        };
    };
    
}
