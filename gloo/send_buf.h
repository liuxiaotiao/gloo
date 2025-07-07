#pragma once

#include <algorithm>
#include <stdlib.h>
#include <numeric>
#include <cstddef>    
#include <cstdint>    
#include <vector>     
#include <stdexcept>  
#include <iostream>   
#include <string>     
#include <algorithm> 
// #pragma message("DEBUG: included span in FILENAME")
#include <immintrin.h>
#include "tool.h"
#include "allreduce.h"

namespace dmludp{
    enum class Channel : uint8_t {
        Unimportant = 0,     
        Important = 1,  
    };

    /* packet flag */
    enum class PktStatus : uint8_t {
        Important_reliable = 1, /* Important packet, Reliable channel */
        Important_reliable_special = 2, /* Important packet, Reliable channel with Unimportant complete info */
        Unimportant_reliable = 3,  /* Unimportant packet, Reliable channel */
        Unimportant_unreliable = 4, /* Unimportant packet, Unreliable channel */
        Unimportant_partialreliable = 5 /* Unimportant packet, cantian important(transmission complete) */
    };
    
    /* send_buf flag */
    enum class MetaFlag : uint8_t {
        /* Important channel status*/
        Initial = 1,       
        Retransmission = 2,
        Complete = 3,
        /* Unimportant channel status*/
        Initial_unimportance = 4,
        Retransmission_unimportance = 5,
        Complete_unimportance = 6, /* Just transmission complete*/
        Ack_complete_unimportance = 7, /* Real unimportant packet transmission complete, receiver know unimportant channel info*/
        Undifine = 16
    };

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
            SendBufferCircularQueue(size_t capacity = 65536) 
                : CircularQueue<uint64_t>(capacity)
            {}

            void push_back(uint64_t value) override {
                data_[tail_] = value;
                tail_ = (tail_ + 1) % capacity_;
            }

            void clear() override {
                head_ = tail_ = 0;
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

        BitmapSpan importance_bitmap;

        /* Important queue info */
        SendBufferCircularQueue rcq_important;

        MetaFlag meta_status_important = MetaFlag::Initial;

        size_t ack_count_important = 0;

        size_t packet_count_important = 0;

        /* First transmission ack check flag */
        bool acknowldge_status_important = true; 

        /* Unimportant queue info */
        SendBufferCircularQueue rcq_unimportant;

        MetaFlag meta_status_unimportant = MetaFlag::Initial_unimportance;

        size_t ack_count_unimportant = 0;

        size_t packet_count_unimportant = 0;

        bool loss_unimportance = false; /* unimportant packet loss record*/

        /* First transmission ack check flag */
        bool acknowldge_status_unimportant = true;
        
        /* Record all retransmission data received info */
        DynamicBitset bits_set;

        /* Packet maximum payload */
        size_t send_buffer_size;

        public:
        /* Important part */
        uint64_t lastpacketOffset_important = std::numeric_limits<uint64_t>::max();

        bool lastlossOffset_important = false;

        size_t initlosscount_important = 0;

        uint64_t lossreord_important = std::numeric_limits<uint64_t>::max();

        /* Unimportant part */
        uint64_t lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();

        size_t initlosscount_unimportant = 0;

        uint64_t lossreord_unimportant = std::numeric_limits<uint64_t>::max();

        ssize_t importantIndex = -1;

        ssize_t unimportantIndex = -1;

        size_t debugcount = std::numeric_limits<size_t>::max();

        std::vector<uint64_t> bitMapVector;

        SendBuf(size_t packet_len): 
        send_buffer_size(packet_len){
            bitMapVector.reserve(64);
        };

        ~SendBuf(){};

        bool is_empty() {
            if (rcq_important.empty() && rcq_unimportant.empty() && meta_left <= 0) return true;
            return false;
        }

        size_t sentComplete(){
            return meta_sent;
        }

        void add_Meta(uint64_t difference, struct iovec* iovecs, int iovecs_len, Span<const uint64_t> bitmapview = {}, uint64_t start_index = 0, uint64_t end_index = 0){
            // meta_status = MetaFlag::Initial;
            meta_sent = 0;

            /* Meta data */
            meta_ptr = nullptr;
            meta_ptr_len = 0;
            meta_ptr2 = nullptr;
            meta_ptr2_len = 0; 
            meta_left = 0;
            meta_len = 0;

            // if (!bitmapview.empty()){
            //     for (auto i = 0 ; i < bitmapview.size() ; i++) {
            //         std::cout<<bitmapview[i]<<" ";
            //     }
            //     std::cout<<std::endl;
            // }
            debugcount = difference;
            // std::cout<<debugcount<<", add_Meta: iovecs_len: " << iovecs_len << ", bitmapview.size(): " << bitmapview.size() << ", bitmapview.data(): " <<bitmapview.data()
            //     << ", start_index: " << start_index << ", end_index: " << end_index << std::endl;
            

            bitMapVector.clear();
            bitMapVector.resize(bitmapview.size());
            memcpy(bitMapVector.data(), bitmapview.data(), bitmapview.size() * sizeof(uint64_t));
            for (auto i = 0; i < bitMapVector.size(); i++) {
                std::cout<<(uint64_t)bitMapVector[i]<<" ";
            }
            std::cout<<std::endl;

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

            // std::cout<<"add_Meta: meta_ptr_len: " << meta_ptr_len << ", meta_ptr2_len: " << meta_ptr2_len 
            //     << ", meta_left: " << meta_left << ", meta_len: " << meta_len << std::endl;
            
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

            lastlossOffset_important = false;
            initlosscount_important = 0;
            lossreord_important = std::numeric_limits<uint64_t>::max();

            loss_unimportance = false;

            importance_bitmap.reset_span(Span<uint64_t>(bitMapVector.data(), bitMapVector.size()), start_index, end_index);

            if (iovecs_len != 1) {
                /*
                1. Only important: importantIndex(!0), unimportantIndex(-1)
                2. Only unimportant: importantIndex(-1), unimportantIndex(!0)
                3. Both important and unimportat: importantIndex(!0), unimportantIndex(!0)
                */
                // auto importantIndex = bitmap.find_last_1_and_0();
                // auto unimportantIndex = bitmap.last_zero();
                if (bitmapview.empty()) {
                    lastpacketOffset_important = 48 + (meta_ptr2_len / MAX_SEND_UDP_PAYLOAD_SIZE) * MAX_SEND_UDP_PAYLOAD_SIZE;

                    /* unimportant part */
                    rcq_unimportant.clear();
                    ack_count_unimportant = 0;
                    packet_count_unimportant = 0;
                    acknowldge_status_unimportant = false;
                    meta_status_unimportant = MetaFlag::Ack_complete_unimportance;

                    lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();
                    initlosscount_unimportant = 0;
                    lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                    packet_count_important = meta_len;
                } else {
                    BitPos pos = importance_bitmap.find_last_1_and_0();
                    // std::cout<<"pos.last_one:"<< pos.last_one <<", " <<  pos.last_zero<<", "<<start_index<<", "<<end_index<<std::endl;
                    importantIndex = pos.last_one;
                    unimportantIndex = pos.last_zero;

                    if (importantIndex != -1 && unimportantIndex == -1){
                        lastpacketOffset_important = 48 + importantIndex * MAX_SEND_UDP_PAYLOAD_SIZE;

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

                        lastpacketOffset_unimportant = 48 + unimportantIndex * MAX_SEND_UDP_PAYLOAD_SIZE;
                        initlosscount_unimportant = 0;
                        lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                    } else {
                        lastpacketOffset_important = 48 + importantIndex * MAX_SEND_UDP_PAYLOAD_SIZE;

                        /* unimportant part */
                        rcq_unimportant.clear();
                        ack_count_unimportant = 0;
                        packet_count_unimportant = 0;
                        acknowldge_status_unimportant = true;
                        meta_status_unimportant = MetaFlag::Initial_unimportance;

                        lastpacketOffset_unimportant = 48 + unimportantIndex * MAX_SEND_UDP_PAYLOAD_SIZE;
                        initlosscount_unimportant = 0;
                        lossreord_unimportant = std::numeric_limits<uint64_t>::max();
                    }
                }
            } else {
                /* Just control message */
                lastpacketOffset_important = 0;

                /* Unimportant part setting */
                lastpacketOffset_unimportant = std::numeric_limits<uint64_t>::max();
                rcq_unimportant.clear();
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance; /* No unimportant packet need to transmission */
            }

            // std::cout<<"add_Meta:"<< static_cast<uint32_t>(meta_status_important) << ", " << static_cast<uint32_t>(meta_status_unimportant)
            //     <<", " << importance_bitmap.empty() << ", " << iovecs_len << ", " << bits_set.size() << ", " << bitmapview.size() 
            //     <<", " << importantIndex << ", "<< unimportantIndex << std::endl;
        } 

        ssize_t off_front_important(){
            ssize_t off = -1;

            /*Special unimportance no loss, both importanct and umimportance complete.*/
            if (off == -1 && meta_status_important == MetaFlag::Complete 
            && meta_status_unimportant == MetaFlag::Complete_unimportance){
                std::cout<<"check 1"<<std::endl;
                if (!rcq_unimportant.empty()) {
                    std::cout<<"check 2"<<std::endl;
                    off = rcq_unimportant.pop_front();
                    return -2;
                }   
            }
            
            if (importance_bitmap.empty()) {
                if(meta_status_important == MetaFlag::Initial){
                    if (meta_left > 0){
                        if (meta_pos == 0){
                            off = meta_pos * send_buffer_size;
                            meta_left -= 48;
                        }else{
                            off = (meta_pos - 1) * send_buffer_size + 48;
                            meta_left -= send_buffer_size;
                        }
                        packet_count_important++;
                        meta_pos++;
                        if (meta_left <= 0){
                            meta_left = 0;
                            meta_status_important = MetaFlag::Retransmission;
                        }
                    }
                }else if(meta_status_important == MetaFlag::Retransmission){
                    while (!rcq_important.empty()) {
                        uint64_t tmp_off = rcq_important.pop_front();
                        if (tmp_off == ELICIT_OFFSET){
                            if (meta_status_unimportant == MetaFlag::Ack_complete_unimportance) {
                                off = -1;
                            } else {
                                off = -2;
                                break;
                            }
                        } else {
                            auto index = 0;            
                            if (tmp_off != 0){
                                index = round_up((tmp_off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
                            }
                            // std::cout<<"1 index;"<<index<<std::endl;
                            if (bits_set[index] == 1){
                                off = -1;
                            } else {
                                off = tmp_off;
                                break;
                            }
                        }
                    }
                } 
            } else {
                if(meta_status_important == MetaFlag::Initial){
                    if (meta_left > 0){
                        if (meta_pos == 0){
                            off = meta_pos * send_buffer_size;
                            meta_left -= 48;
                            packet_count_important++;
                            // std::cout<<debugcount<<", off_front_important: off:"<<off<<", meta_left:"<<meta_left<<", index: -1"<<std::endl;
                            if (importantIndex == -1) {
                                meta_status_important = MetaFlag::Retransmission;
                            }
                        }else{
                            auto index = importance_bitmap.next_one_avx512();
                            // std::cout<<"index:"<<index<<", "<<importantIndex<<std::endl;
                            
                            if (index != -1) {
                                off = index * send_buffer_size + 48;
                                meta_left -= send_buffer_size;
                                packet_count_important++;
                                // std::cout<<debugcount<<", off_front_important: off:"<<off<<", meta_left:"<<meta_left<<", index:"<<index<<std::endl;
                                if (index == importantIndex) {
                                    meta_status_important = MetaFlag::Retransmission;
                                }
                            } else {  
                                std::cout<< "off_front_important index could not be -1," << unimportantIndex << ", " << static_cast<uint32_t>(meta_status_unimportant) << std::endl;
                                _Exit(0);
                            }  
                        }
                        meta_pos++;
                    }
                }else if(meta_status_important == MetaFlag::Retransmission){
                    while (!rcq_important.empty()) {
                        uint64_t tmp_off = rcq_important.pop_front();
                        if (tmp_off == ELICIT_OFFSET){
                            if (meta_status_unimportant == MetaFlag::Ack_complete_unimportance) {
                                off = -1;
                            } else {
                                off = -2;
                                break;
                            }
                        } else {
                            auto index = 0;            
                            if (tmp_off != 0){
                                index = round_up((tmp_off - 48), MAX_SEND_UDP_PAYLOAD_SIZE) + 1;
                            }
                            // std::cout<<"2 index;"<<index<<std::endl;
                            if (bits_set[index] == 1){
                                off = -1;
                            } else {
                                off = tmp_off;
                                break;
                            }
                        }
                    }
                } 
            }
            
            return off;
        }

        ssize_t off_front_unimportant (PktStatus & packet_status) {
            packet_status = PktStatus::Unimportant_reliable;
            ssize_t off = -1;
            if (meta_status_unimportant == MetaFlag::Initial_unimportance){
                /* Unimportant part first transmission */
                if (meta_left > 0){
                    auto index = importance_bitmap.next_zero_avx512(); 
                    if (index != -1) {
                        off = index * send_buffer_size + 48;
                        meta_left -= send_buffer_size;
                        if (meta_left < 0) {
                            meta_left = 0;
                        }
                        // std::cout<<debugcount<<", off_front_unimportant: off:"<<off<<", meta_left:"<<meta_left<<", index:"<<index<<std::endl;
                        if (off == lastpacketOffset_unimportant) {
                            meta_status_unimportant = MetaFlag::Retransmission_unimportance;
                        }
                    } else {
                        std::cout<<"off_front_unimportant error:"<<importance_bitmap.empty()<<", "<<bits_set.size()<<", "<< static_cast<uint32_t>(meta_status_unimportant) <<std::endl;
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
                    // std::cout<<"3 index;"<<index<<std::endl;
                    if (bits_set[index] == 1){
                        off = -1;
                        /* TODO: Merge lastlossOffset_unimportant and acknowldge_status_unimportant */
                        if (rcq_unimportant.empty() && !acknowldge_status_unimportant/* ack first transmission packet mark it as true*/){
                            // lastlossOffset_unimportant = false;
                            if (loss_unimportance) {
                                /* has packet loss, should send extra elicit packet */
                                std::cout<<"1 MetaFlag::Complete_unimportance"<<std::endl;
                                meta_status_unimportant = MetaFlag::Complete_unimportance;
                                off = -2;
                                packet_status = PktStatus::Important_reliable;
                            } else {
                                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
                                off = -1;
                            }
                        }
                    } else { 
                        if (!loss_unimportance) {
                            loss_unimportance = true;
                        }
                        if (rcq_unimportant.empty() && !acknowldge_status_unimportant/* ack first transmission packet mark it as true*/){
                            // lastlossOffset_unimportant = false;
                            std::cout<<"2 MetaFlag::Complete_unimportance"<<std::endl;
                            meta_status_unimportant = MetaFlag::Complete_unimportance;
                            packet_status = PktStatus::Unimportant_partialreliable;
                            /* Merge elicit flag to last loss packet */
                        } else {
                            packet_status = PktStatus::Unimportant_unreliable;
                        }
                        break;
                    }
                } 
            } 
            return off;
        }

        void acknowledege_and_drop(uint64_t in_offset, bool is_drop, bool realack){
            if (realack) {
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
            }

            /* Elicit packet */
            if (in_offset == ELICIT_OFFSET && is_drop) {
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
                return;
            }

            if (in_offset == ELICIT_OFFSET && !is_drop) {
                rcq_important.push_back(ELICIT_OFFSET);
                return;
            }

            if (is_drop){ /* received */
                if (acknowldge_status_important) {
                    if (in_offset == lastpacketOffset_important) {
                        if (initlosscount_important > packet_count_important) {
                            std::cout<<"1. initlosscount_important(" << initlosscount_important << ") > packet_count_important(" << packet_count_important << ")" <<std::endl;
                            _Exit(0);
                        }
                        
                        ack_count_important = packet_count_important - initlosscount_important;
                        acknowldge_status_important = false;
                    }
                } else {
                    if (in_offset != ELICIT_OFFSET){
                        auto index = 0;
                        if (in_offset >= 48){
                            index = (in_offset - 48) / send_buffer_size + 1;
                        }
                        // std::cout<<"4 index;"<<index<<std::endl;
                        if (bits_set[index] == 0){
                            bits_set.set(index);
                            ack_count_important++;
                        }
                    } else {
                        meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
                    }
                }
            }else{ /* loss */
                if (acknowldge_status_important){
                    if (!rcq_important.empty()){
                        if (in_offset != ELICIT_OFFSET){
                            if (in_offset > rcq_important.back()) {
                                auto index = 0;
                                if (in_offset >= 48){
                                    index = (in_offset - 48) / send_buffer_size + 1;
                                }
                                // std::cout<<"5 index;"<<index<<std::endl;
                                if (bits_set[index] == 0){
                                    rcq_important.push_back(in_offset);
                                    if (lossreord_important == std::numeric_limits<uint64_t>::max() && in_offset != ELICIT_OFFSET){
                                        lossreord_important = in_offset;
                                        // std::cout<<"1. in_offset:"<<in_offset<<std::endl;
                                        ++initlosscount_important;
                                    } else {
                                        if (in_offset > lossreord_important && in_offset != ELICIT_OFFSET) {
                                            // std::cout<<"2. in_offset:"<<in_offset<<std::endl;
                                            ++initlosscount_important;
                                            lossreord_important = in_offset;
                                        }
                                    }
                                    
                                }
                            }
                        } else {
                            rcq_important.push_back(ELICIT_OFFSET);
                        }
                    }else {
                        if (in_offset != ELICIT_OFFSET){
                            auto index = 0;
                            if (in_offset >= 48){
                                index = (in_offset - 48) / send_buffer_size + 1;
                            }
                            // std::cout<<"6 index;"<<index<<std::endl;
                            if (bits_set[index] == 0){
                                rcq_important.push_back(in_offset);
                                /* TODO: if out of order */
                                if (lossreord_important == std::numeric_limits<uint64_t>::max() && in_offset != ELICIT_OFFSET){
                                    lossreord_important = in_offset;
                                    // std::cout<<"3. in_offset:"<<in_offset<<std::endl;
                                    ++initlosscount_important;
                                } else {
                                    if (in_offset > lossreord_important && in_offset != ELICIT_OFFSET) {
                                        // std::cout<<"4. in_offset:"<<in_offset<<std::endl;
                                        ++initlosscount_important;
                                        lossreord_important = in_offset;
                                    }
                                }
                                
                            }
                        }else{
                            rcq_important.push_back(ELICIT_OFFSET);
                        }
                    }

                    if (in_offset == lastpacketOffset_important) {
                        if (initlosscount_important > packet_count_important) {
                            std::cout<<"2. initlosscount_important(" << initlosscount_important << ") > packet_count_important(" << packet_count_important << ")" <<std::endl;
                            _Exit(0);
                        }
                        // std::cout<<"2. packet_count_important: "<< packet_count_important <<", " <<initlosscount_important<<std::endl;
                        ack_count_important = packet_count_important - initlosscount_important;
                        acknowldge_status_important = false;
                    }
                }else {
                    if (in_offset != ELICIT_OFFSET) {

                        auto index = 0;
                        if (in_offset >= 48){
                            index = (in_offset - 48) / send_buffer_size + 1;
                        }
                        if (bits_set[index] == 0){
                            rcq_important.push_back(in_offset);
                        }
                    } else{
                        rcq_important.push_back(ELICIT_OFFSET);
                    }
                }
                
            }

            
            if (ack_count_important == packet_count_important){
                meta_status_important = MetaFlag::Complete;
                if (meta_status_unimportant == MetaFlag::Complete_unimportance) {
                    /* Both important and unimportant complete */
                    rcq_important.push_back(ELICIT_OFFSET);
                } 
            }       
        }


        void acknowledege_and_drop_unimportant(uint64_t in_offset, bool is_drop, bool realack){
            if (realack) {
                meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
                return;
            }

            if (acknowldge_status_unimportant) {
                if (is_drop) {
                    if (in_offset == lastpacketOffset_unimportant) {
                        ack_count_unimportant = packet_count_unimportant - initlosscount_unimportant;
                        acknowldge_status_unimportant = false;
                        if (initlosscount_unimportant == 0) {
                            std::cout<<"3 MetaFlag::Complete_unimportance"<<std::endl;
                            meta_status_unimportant = MetaFlag::Complete_unimportance;
                            rcq_important.push_back(ELICIT_OFFSET);
                        }
                    }
                } else {
                    if (!rcq_unimportant.empty()){
                        if (in_offset > rcq_unimportant.back()) {
                            auto index = 0;
                            if (in_offset >= 48){
                                index = (in_offset - 48) / send_buffer_size + 1;
                            }
                            // std::cout<<"8 index;"<<index<<std::endl;
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
                        }
                        // std::cout<<"9 index;"<<index<<std::endl;
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
                        if (initlosscount_unimportant == 0) {
                            std::cout<<"4 MetaFlag::Complete_unimportance"<<std::endl;
                            meta_status_unimportant = MetaFlag::Complete_unimportance;
                            rcq_important.push_back(ELICIT_OFFSET);
                        }
                    }   
                }
            } else {
                auto index = 0;
                if (in_offset >= 48){
                    index = (in_offset - 48) / send_buffer_size + 1;
                }  
                // std::cout<<"10 index;"<<index<<std::endl;
                if (bits_set[index] == 0){
                    bits_set.set(index);
                    ack_count_unimportant++;
                }
            }

            // if (ack_count_unimportant == packet_count_unimportant && packet_count_unimportant != 0) {
            //     meta_status_unimportant = MetaFlag::Ack_complete_unimportance;
            // }  
            
        }

        size_t round_up(uint64_t a, uint64_t b){
            if (b == 0) {
                throw std::invalid_argument("Division by zero is not allowed");
            }

            return (a + b - 1) / b;
        }

        void ack_check(){
            std::cout << "ack_count:" << ack_count_important << ", "<<packet_count_important<< ", " << bits_set.size() << ", " << bits_set.count() <<", "<< rcq_important.size() <<
            ", " << static_cast<uint32_t>(meta_status_unimportant) << ", "<< packet_count_important << ", "<< static_cast<uint32_t>(meta_status_important) <<
            ", " << lastpacketOffset_important << ", " << lastpacketOffset_unimportant << ", " << initlosscount_important << ", " << meta_ptr_len + meta_ptr2_len 
            << std::endl;
            // for (auto i = 0; i < bitMapVector.size(); i++) {
            //     std::cout<<(int)bitMapVector[i]<<" ";
            // }
            // std::cout<<std::endl;
        }


        bool emit(
            struct iovec& out, ssize_t& out_len, uint64_t& out_off, 
            uint16_t & blocks /* Important packet count */, 
            uint8_t & status_ /* Contain unimportant completeness */
            ){

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
                    out.iov_len = out_len;
                }  
            } else {
                /* Special(Elicitack) */
                out_off = ELICIT_OFFSET;
                out_len = 0;
                out.iov_base = nullptr;
                out.iov_len = 0;
            }
                

            if (meta_status_important != MetaFlag::Initial) {
                if (importance_bitmap.empty()) {
                    blocks = NO_UNIMPORTANT_BLOCK;
                } else {
                    blocks = packet_count_important;
                }
            } else {
                if (importance_bitmap.empty()) {
                    blocks = NO_UNIMPORTANT_BLOCK;
                } else {
                    blocks = BLOCK_DEFAULT;
                }
            }

            /* Type: Application2 */
            if (meta_status_unimportant == MetaFlag::Complete_unimportance || meta_status_unimportant == MetaFlag::Ack_complete_unimportance) {
                status_ = 1;
            } else {
                status_ = 0;
            }

            return stop;
        }


        bool emit_unimportance(struct iovec& out, ssize_t& out_len, uint64_t& out_off, uint16_t & blocks, PktStatus & status_ /* Contain completeness */){
            bool stop = false;
            // std::cout << debugcount<<", emit_unimportance1: meta_status_unimportant: " << static_cast<uint32_t>(meta_status_unimportant) << std::endl;
            if (meta_status_unimportant == MetaFlag::Ack_complete_unimportance || meta_status_unimportant == MetaFlag::Complete_unimportance) {
                out_len = -1;
                stop = true;
                return stop;
            }
            
            out_len = 0;
            auto tmp_off = off_front_unimportant(status_);
            // std::cout << debugcount<<", emit_unimportance2: tmp_off: " << tmp_off << ", status_: " << static_cast<uint32_t>(status_) << std::endl;

            if (tmp_off == -1){
                out_len = -1;
                stop = true;
                return stop;
            }

            if (tmp_off == -2) {
                out_off = ELICIT_OFFSET;
            }

            out_off = tmp_off;
            if (out_off == 0){
                /* offset 0 cannot be unimportant */
                out_len = meta_ptr_len;
                out.iov_base = meta_ptr;
                out.iov_len = meta_ptr_len;
            } else if (out_off == ELICIT_OFFSET) {
                out_len = 0;
                out.iov_base = nullptr;
                out.iov_len = 0;
            }else{
                out_len = std::min(send_buffer_size, size_t(meta_ptr2_len - (out_off - 48)));
                out.iov_base = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(meta_ptr2) + out_off - 48);
                out.iov_len = out_len;
            }      

            if (meta_status_important != MetaFlag::Initial) {
                blocks = packet_count_important;
            } else {
                blocks = BLOCK_DEFAULT;
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
