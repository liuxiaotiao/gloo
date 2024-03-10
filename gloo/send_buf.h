#pragma once

#include <deque>
#include "gloo/RangeBuf.h"
#include <algorithm>
#include <unordered_set>

namespace dmludp{
const size_t SEND_BUFFER_SIZE = 1350;

const size_t MIN_SENDBUF_INITIAL_LEN = 1350;

    class SendBuf{
        public:
        // std::deque<std::shared_ptr<RangeBuf>> data;
        // Date: 8th Jan, 2024
        // Data pair store offset 
        std::deque<std::pair<uint64_t,std::pair<uint8_t*, uint64_t>>> data;

        size_t pos;

        uint64_t off;

        uint64_t length;

        uint64_t max_data;

        // error

        size_t used_length;

        std::map<uint64_t, bool> offset_recv;

        uint64_t removed;

        size_t sent;

        std::unordered_set<uint64_t> recv_count;

        SendBuf():
        pos(0),
        off(0),
        length(0),
        max_data(MIN_SENDBUF_INITIAL_LEN * 8),
        used_length(0),
        removed(0),
        sent(0){};

        ~SendBuf(){};

        ssize_t cap(){
            used_length = len();
            return ((ssize_t)max_data - (ssize_t)used_length);
        };

        /// Returns the lowest offset of data buffered.
        uint64_t off_front() {
            auto tmp_pos = pos;

            while (tmp_pos <= (data.size() - 1)){
                auto b = data.at(tmp_pos);
                // if(!(b->is_empty())){
                //     return b->off();
                // }
                if(b.second.second != 0){
                    return b.first;
                }
                tmp_pos += 1;
            }

            // return off;
        }

        /// Returns true if there is data to be written.
        bool ready(){
            return !data.empty();
        };


        void clear(){
            data.clear();
            pos = 0;
            off = 0;
            length = 0;
        };

        /// Returns the largest offset of data buffered.
        uint64_t off_back(){
            return off;
        };

        /// The maximum offset we are allowed to send to the peer.
        uint64_t max_off() {
            return max_data;
        };

        bool is_empty(){
            return data.empty();
        };

        // Length of stored data.
        size_t len(){
            size_t length = 0;
            if (data.empty()){
                return 0;
            }

            for (auto x : data) {
                length += x.second.second;
            }

            return length;
        };
        /// Returns true if the stream was stopped before completion.
        // bool is_stopped(){
        //     self.error.is_some()
        // }

        /// Updates the max_data limit to the given value.
        void update_max_data(uint64_t maxdata) {
            max_data = maxdata;
        };

        ////rewritetv
        /// Resets the stream at the current offset and clears all buffered data.
        uint64_t reset(){
            auto unsent_off = off_front();
            auto unsent_len = off_back() - unsent_off;

            // Drop all buffered data.
            data.clear();

            pos = 0;
            length = 0;
            off = unsent_off;

            return unsent_len;
        };

        // Mark received data as false.
        void ack_and_drop(uint64_t offset){
            auto it = offset_recv.find(offset);
            if (it != offset_recv.end()) {
                it->second = false;
            }
        };

        void insert_ack(uint64_t offset){
            auto it = offset_recv.find(offset);
            if (it != offset_recv.end()) {
                recv_count.insert(offset);
            }
        };

        bool check_ack(){
            return (recv_count.size() == offset_recv.size()) ;
        };

        // Remove received data from send buffer.
        void recv_and_drop() {
            if (data.empty()) {
                return;
            }

            std::set<uint64_t> keysToDelete;

            for (const auto& kv : offset_recv) {
                if (!kv.second) { 
                    keysToDelete.insert(kv.first);
                }
            }
          
            data.erase(
                std::remove_if(
                    data.begin(), 
                    data.end(),
                    [&keysToDelete](const std::pair<uint64_t, std::pair<uint8_t*, uint64_t>>& element) {
                        return keysToDelete.find(element.first) != keysToDelete.end();
                    }
                ), 
                data.end()
            );

            for (const auto& key : keysToDelete) {
                offset_recv.erase(key);
            }
        };
        

        size_t pkt_num(){
            return data.size();
        }
        
        // Explain: 
        // off_len = SEND_BUFFER_SIZE - sent_partial_off
        // write_data_len = length of all data
        ssize_t write(uint8_t* src, size_t start_off, size_t &write_data_len, size_t window_size, size_t off_len) {
            // All data in the buffer has been sent out, remove received data from the buffer.
            if (pos == 0) {
                //recv_and_drop();
                recv_count.clear();
            }

            max_data = (uint64_t)window_size;
            auto capacity = cap();
            if (capacity <= 0) {
                return 0;
            }

            // All data has been written into buffer.
            if (write_data_len <= 0){
                return -1;
            }

            if (is_empty()){
                // max_data = (uint64_t)window_size;
                removed = 0;
                sent = 0;
                // Get the send capacity. This will return an error if the stream
                // was stopped.
                length = (uint64_t)len();
                used_length = len();
                //Addressing left data is greater than the window size
                if (length >= window_size){
                    return 0;
                }
        
                if (write_data_len == 0){
                    return 0;
                }
                                
                size_t ready_written = 0;
                if (write_data_len > capacity){
                    ready_written = capacity;
                }else{
                    ready_written = write_data_len;
                }
        
                // We already recorded the final offset, so we can just discard the
                // empty buffer now.
        
                size_t write_len = 0;

                // off_len represnts the length of a splited data
                if (off_len > 0){
                    if ( ready_written > off_len ){
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off, off_len)));
                        off += (uint64_t)off_len;
                        length += (uint64_t)off_len;
                        used_length += off_len;
                        write_len += off_len;
                    }
                    else{
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off, ready_written )));
                        off += (uint64_t)ready_written;
                        length += (uint64_t)ready_written;
                        used_length += ready_written;
                        write_len += ready_written;

                        write_data_len -= write_len;
                        return write_len;
                    }
                }
        
                for ( auto it = off_len; it < ready_written; ){
                    if ((ready_written - it) > SEND_BUFFER_SIZE){
                        write_len += SEND_BUFFER_SIZE;
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off + it, SEND_BUFFER_SIZE)));
                        off += (uint64_t)SEND_BUFFER_SIZE;
                        length += (uint64_t)SEND_BUFFER_SIZE;
                        used_length += SEND_BUFFER_SIZE;
                        it += SEND_BUFFER_SIZE;
                    }else{
                        write_len += ready_written - it;
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off + it, ready_written - it)));
                        off += (uint64_t)(ready_written - it);
                        length += (uint64_t)(ready_written - it);
                        used_length += (ready_written - it);
                        it = ready_written;
                    }
                    
                }  
                write_data_len -= write_len;
                return write_len;   
            }
            else{
                // Get the stream send capacity. This will return an error if the stream
                // was stopped.
                // length = (uint64_t)len();
                // used_length = len();
                //Addressing left data is greater than the window size
                if (len() >= window_size){
                    return 0;
                }
        
                if ( write_data_len == 0){
                    return 0;
                }

                size_t ready_written = 0;
                if ( write_data_len > capacity){
                    ready_written = capacity;
                }else{
                    ready_written = write_data_len;
                }

                size_t write_len = 0;

                if (off_len > 0){
                    if ( ready_written > off_len ){
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off, (uint64_t)off_len)));
                        off += (uint64_t)off_len;
                        length += (uint64_t)off_len;
                        used_length += off_len;
                        write_len += off_len;
                        // offset_recv.insert(off, true);
                    }
                    else{
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off, (uint64_t)ready_written )));
                        offset_recv[off] = true;
                        off += (uint64_t)ready_written;
                        length += (uint64_t)ready_written;
                        used_length += ready_written;
                        write_len += ready_written;

                        write_data_len -= write_len;
                        // offset_recv.insert(off, true);
                        return write_len;
                    }
                }

                for (auto it = off_len ; it < ready_written ; ){
                    if((ready_written - it) > SEND_BUFFER_SIZE){
                        write_len += SEND_BUFFER_SIZE;
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off + it, SEND_BUFFER_SIZE)));
                        off += (uint64_t) SEND_BUFFER_SIZE;
                        length += (uint64_t) SEND_BUFFER_SIZE;
                        used_length += SEND_BUFFER_SIZE;
                        it += SEND_BUFFER_SIZE;
                        // offset_recv.insert(off, true);
                    }else{
                        write_len += (ready_written - it);
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(off, std::make_pair(src + start_off + it, (uint64_t)(ready_written - it))));
                        off += (uint64_t) (ready_written - it);
                        length += (uint64_t) (ready_written - it);
                        used_length += (ready_written - it);
                        it = ready_written;
                        // offset_recv.insert(off, true);
                    }

                    
                }
                write_data_len -= write_len;
                return write_len;
            }
    
        };

        bool emit(struct iovec & out, size_t& out_len, uint64_t& out_off){
            bool stop = false;
            out_len = 0;
            out_off = off_front();
            while ( ready () ){
                if(pos >= data.size()){
                    break;
                }

                auto buf = data.at(pos);

                if (buf.second.second == 0){
                    pos += 1;
                    continue;
                }
                
                size_t buf_len = 0;
                
                bool partial;
                if( buf.second.second <= MIN_SENDBUF_INITIAL_LEN){
                    partial = true;
                }else{
                    partial = false;
                }

                // Copy data to the output buffer.
                out.iov_base = (void *)(buf.second.first);
                out.iov_len = buf.second.second;

                length -= (uint64_t)(buf.second.second);
                used_length -= (buf.second.second);

                out_len = (buf.second.second);

                //buf.consume(buf_len);
                pos += 1;

                if (partial) {
                    // We reached the maximum capacity, so end here.
                    break;
                }

            }
            sent += out_len;

            //All data in the congestion control window has been sent. need to modify
            if (sent  >= max_data) {
                stop = true;
                pos = 0;
            }
            if (pos == data.size()){
                stop = true;
                pos = 0;
            }

            out_off += out_len;
            return stop;
        };

    };
    
}
