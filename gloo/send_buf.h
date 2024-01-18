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
        std::deque<std::pair<uint8_t*, uint64_t>> data;

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

        size_t cap(){
            used_length = len();
            return (size_t)(max_data - (uint64_t)used_length);
        };

        /// Returns the lowest offset of data buffered.
        uint64_t off_front() {
            auto tmp_pos = pos;

            while (tmp_pos <= (data.size() - 1)){
                auto b = data.at(tmp_pos);
                // if(!(b->is_empty())){
                //     return b->off();
                // }
                if(b.second != 0){
                    return off;
                }
                tmp_pos += 1;
            }

            return off;
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
                length += x.second;
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
		    std::cout<<"data.empty()"<<std::endl;
                return;
            }
	    std::cout<<"data.size(): "<<data.size()<<" "<<data.empty()<<std::endl;

            // Note: off defines the start offset of the buffer.
            size_t currentIndex = 0; 
            for (auto it = offset_recv.begin(); it != offset_recv.end(); /* no increment here */){
                if (it->second == true){
                    data[currentIndex++] = data[it->first];
                    it = offset_recv.erase(it);
                }else{
                    ++it;
                }
            }
            data.resize((currentIndex - 1));
        };
        
        // From protocal to scoket
        // size_t write(std::vector<uint8_t> wrtie_data, size_t window_size, size_t off_len, uint64_t _max_ack) {
        //     // All data in the buffer has been sent out, remove received data from the buffer.
        //     if (pos == 0) {
        //         recv_and_drop();
        //         recv_count.clear();
        //     }

        //     auto capacity = cap();
        //     if (capacity == 0) {
        //         return 0;
        //     }

        //     if (is_empty()){
        //         max_data = (uint64_t)window_size;
        //         removed = 0;
        //         sent = 0;
        //         // Get the send capacity. This will return an error if the stream
        //         // was stopped.
        //         length = (uint64_t)len();
        //         used_length = len();
        //         //Addressing left data is greater than the window size
        //         if (length >= window_size){
        //             return 0;
        //         }
        
        //         if (wrtie_data.size() == 0){
        //             return 0;
        //         }
                
        //         std::vector<uint8_t> tmp_data;

        //         if (wrtie_data.size() > capacity) {
        //             // Truncate the input buffer according to the stream's capacity.
        //             auto local_len = capacity;
        //             tmp_data.assign(wrtie_data.begin(), wrtie_data.begin()+local_len);  
        //             // data = data.resize(len);
        //         }else{
        //             tmp_data.assign(wrtie_data.begin(), wrtie_data.end());  
        //         }
        
        //         // We already recorded the final offset, so we can just discard the
        //         // empty buffer now.
        //         if (tmp_data.empty()) {
        //             return tmp_data.size();
        //         }
        
        //         size_t write_len = 0;
        
        //         if (off_len > 0) {
        //             if (tmp_data.size() > off_len){
        //                 std::vector<uint8_t> slice_data(tmp_data.begin(), tmp_data.begin()+off_len);
        //                 auto first_buf = RangeBuf::from(slice_data, off);
        
        //                 offset_recv[off] = true;
        
        //                 data.push_back(first_buf);
        //                 off += (uint64_t)off_len;
        //                 length += (uint64_t)off_len;
        //                 used_length += off_len;
        //                 write_len += off_len;
        //             }else{

        //                 auto first_buf= RangeBuf::from(tmp_data, off);
        //                 offset_recv[off] = true;
        
        //                 data.push_back(first_buf);
        //                 off += (uint64_t)wrtie_data.size();
        //                 length += (uint64_t)wrtie_data.size();
        //                 used_length += wrtie_data.size();
        //                 write_len += wrtie_data.size();
        //                 return write_len;
        //             }
        
        //         }
                
        //         for (auto it = off_len; it < tmp_data.size();){
        //             if((tmp_data.size() - it) > SEND_BUFFER_SIZE ){
        //                 std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.begin() + it + SEND_BUFFER_SIZE);
        //                 write_len += SEND_BUFFER_SIZE;

        //                 auto buf = RangeBuf::from(slice_data, off);

        //                 offset_recv[off] = true;
            
        //                 // The new data can simply be appended at the end of the send buffer.
        //                 data.push_back(buf);
            
        //                 off += (uint64_t)slice_data.size();
        //                 length += (uint64_t)slice_data.size();
        //                 used_length += slice_data.size();
        //                 it += SEND_BUFFER_SIZE;
        //             }else{
        //                 std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.end());
        //                 write_len += (tmp_data.size() - it);

        //                 auto buf = RangeBuf::from(slice_data, off);

        //                 offset_recv[off] = true;
            
        //                 // The new data can simply be appended at the end of the send buffer.
        //                 data.push_back(buf);
            
        //                 off += (uint64_t)slice_data.size();
        //                 length += (uint64_t)slice_data.size();
        //                 used_length += slice_data.size();

        //                 it = tmp_data.size();
        //             }
        //         }
        //         return write_len;
        //     }
        //     else{
        //         // Get the stream send capacity. This will return an error if the stream
        //         // was stopped.
        //         length = (uint64_t)len();
        //         used_length = len();
        //         //Addressing left data is greater than the window size
        //         if (length >= window_size){
        //             return 0;
        //         }
        
        //         if (wrtie_data.size() == 0){
        //             return 0;
        //         }
            
        //         // let capacity = self.cap()?;

        //         std::vector<uint8_t> tmp_data;

                
        //         if (wrtie_data.size() > capacity) {
        //             // Truncate the input buffer according to the stream's capacity.
        //             auto copy_len = capacity;
        //             std::copy(wrtie_data.begin(), wrtie_data.begin()+copy_len, tmp_data.begin());
        //         }else{
        //             std::copy(wrtie_data.begin(), wrtie_data.end(), tmp_data.begin());

        //         }
        
        //         // We already recorded the final offset, so we can just discard the
        //         // empty buffer now.
        //         if (tmp_data.empty()) {
        //             return data.size();
        //         }
        
        //         size_t write_len = 0;

        //         for (auto it = off_len ; it < tmp_data.size();){
        //             if (tmp_data.size() - it > SEND_BUFFER_SIZE){
        //                 std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.begin() + it + SEND_BUFFER_SIZE);
        //                 write_len += slice_data.size();
        //                 auto buf = RangeBuf::from(slice_data, off);
        //                 offset_recv[off] = true;
        //                 data.push_back(buf);
        //                 off += (uint64_t) wrtie_data.size();
        //                 length += (uint64_t) wrtie_data.size();
        //                 used_length += wrtie_data.size();
        //                 it += SEND_BUFFER_SIZE;
        //             }else{
        //                 std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.end());
        //                 write_len += slice_data.size();
        //                 auto buf = RangeBuf::from(slice_data, off);
        //                 offset_recv[off] = true;
        //                 data.push_back(buf);
        //                 off += (uint64_t) wrtie_data.size();
        //                 length += (uint64_t) wrtie_data.size();
        //                 used_length += wrtie_data.size();
        //                 it = tmp_data.size();
        //             }
        //         }
        //         return write_len;
        //     }
    
        // };

        // // From application to protocal.
        // bool emit(std::vector<uint8_t> &out, size_t& out_len, uint64_t& out_off){
        //     bool stop = false;
        //     out_len = out.size();

        //     out_off = off_front();
        
        //     while (out_len >= SEND_BUFFER_SIZE && ready())
        //     {
        //         if(pos >= data.size()){
        //             break;
        //         }

        //         auto buf = data.at(pos);

        //         if (buf->is_empty()) {
        //             pos += 1;
        //             continue;
        //         }

        //         size_t buf_len = 0;
        //         if (buf->len() > out_len){
        //             buf_len = out_len;
        //         }else{
        //             buf_len = buf->len();
        //         }

        //         bool partial;
        //         if(buf_len <= buf->len()){
        //             partial = true;
        //         }else{
        //             partial = false;
        //         }

        //         // Copy data to the output buffer.
        //         size_t out_pos = 0;
        //         std::copy((buf->data).begin(), (buf->data).begin() + buf_len, out.begin() + out_pos);

        //         length -= (uint64_t)buf_len;
        //         used_length -= buf_len;

        //         out_len = buf_len;

        //         //buf.consume(buf_len);
        //         pos += 1;

        //         if (partial) {
        //             // We reached the maximum capacity, so end here.
        //             break;
        //         }

        //     }
        //     sent += out_len;

        //     //All data in the congestion control window has been sent. need to modify
        //     if (sent  >= max_data) {
        //         stop = true;
        //         pos = 0;
        //     }
        //     if (pos == data.size()){
        //         stop = true;
        //         pos = 0;
        //     }
        //     return stop;
        // };

        size_t pkt_num(){
            return data.size();
        }
        
        // Explain: 
        // off_len = SEND_BUFFER_SIZE - sent_partial_off
        // write_data_len = length of all data
        ssize_t write(uint8_t* src, size_t start_off, size_t &write_data_len, size_t window_size, size_t off_len) {
            // All data in the buffer has been sent out, remove received data from the buffer.
            if (pos == 0) {
                recv_and_drop();
                recv_count.clear();
            }
            auto capacity = cap();
            if (capacity == 0) {
                return 0;
            }

            // All data has been written into buffer.
            if (write_data_len <= 0){
                return -1;
            }

            if (is_empty()){
                max_data = (uint64_t)window_size;
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
                        data.push_back(std::make_pair(src + start_off, off_len));
                        off += (uint64_t)off_len;
                        length += (uint64_t)off_len;
                        used_length += off_len;
                        write_len += off_len;
                    }
                    else{
                        data.push_back(std::make_pair(src + start_off, ready_written ));
                        offset_recv[off] = true;
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
                        data.push_back(std::make_pair(src + start_off + it, SEND_BUFFER_SIZE));
                        off += (uint64_t)SEND_BUFFER_SIZE;
                        length += (uint64_t)SEND_BUFFER_SIZE;
                        used_length += SEND_BUFFER_SIZE;
                        it += SEND_BUFFER_SIZE;
                    }else{
                        write_len += ready_written - it;
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(src + start_off + it, ready_written - it));
                        off += (uint64_t)(ready_written - it);
                        length += (uint64_t)(ready_written - it);
                        used_length += (ready_written - it);
                        it = ready_written;
                    }
                    write_data_len -= write_len;
                    return write_len;
                }     
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
                        data.push_back(std::make_pair(src + off, (uint64_t)off_len));
                        off += (uint64_t)off_len;
                        length += (uint64_t)off_len;
                        used_length += off_len;
                        write_len += off_len;
                        // offset_recv.insert(off, true);
                    }
                    else{
                        data.push_back(std::make_pair(src + off, (uint64_t)ready_written ));
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
                        data.push_back(std::make_pair(src + off, SEND_BUFFER_SIZE));
                        off += (uint64_t) SEND_BUFFER_SIZE;
                        length += (uint64_t) SEND_BUFFER_SIZE;
                        used_length += SEND_BUFFER_SIZE;
                        it += SEND_BUFFER_SIZE;
                        // offset_recv.insert(off, true);
                    }else{
                        write_len += (ready_written - it);
                        offset_recv[off] = true;
                        data.push_back(std::make_pair(src + off, (uint64_t)(ready_written - it)));
                        off += (uint64_t) (ready_written - it);
                        length += (uint64_t) (ready_written - it);
                        used_length += (ready_written - it);
                        it = ready_written;
                        // offset_recv.insert(off, true);
                    }

                    write_data_len -= write_len;
                    return write_len;
                }
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

                if (buf.second == 0){
                    pos += 1;
                    continue;
                }
                
                size_t buf_len = 0;
                
                bool partial;
                if( buf.second <= MIN_SENDBUF_INITIAL_LEN){
                    partial = true;
                }else{
                    partial = false;
                }

                // Copy data to the output buffer.
                out.iov_base = (void *)(buf.first);
                out.iov_len = buf.second;

                length -= (uint64_t)(buf.second);
                used_length -= (buf.second);

                out_len = (buf.second);

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
            return stop;
        };

    };
    
}
