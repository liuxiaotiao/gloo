#pragma once

#include <deque>
#include "gloo/RangeBuf.h"
#include <algorithm>

namespace dmludp{
const size_t SEND_BUFFER_SIZE = 1024;

const size_t MIN_SENDBUF_INITIAL_LEN = 1350;

    class SendBuf{
        public:
        std::deque<std::shared_ptr<RangeBuf>> data;

        size_t pos;

        uint64_t off;

        uint64_t length;

        uint64_t max_data;

        // error

        size_t used_length;

        std::map<uint64_t, bool> offset_recv;

        uint64_t removed;

        size_t sent;

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
                if(!(b->is_empty())){
                    return b->off();
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

        size_t len(){
            size_t length = 0;
            if (data.empty()){
                return 0;
            }

            for (auto x : data) {
                length += (x->data).size();
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

        // pub fn recv_and_drop(&mut self, ack_set: &[u64], unack_set: &[u64],len: usize) {
        // Remove received data from send buffer.
        void recv_and_drop() {
            if (data.empty()) {
                return;
            }

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
        size_t write(std::vector<uint8_t> wrtie_data, size_t window_size, size_t off_len, uint64_t _max_ack) {
            // All data in the buffer has been sent out, remove received data from the buffer.
            if (pos == 0) {
                recv_and_drop();
            }

            auto capacity = cap();
            if (capacity == 0) {
                return 0;
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
        
                if (wrtie_data.size() == 0){
                    return 0;
                }
                
                std::vector<uint8_t> tmp_data;

                if (wrtie_data.size() > capacity) {
                    // Truncate the input buffer according to the stream's capacity.
                    auto local_len = capacity;
                    tmp_data.assign(wrtie_data.begin(), wrtie_data.begin()+local_len);  
                    // data = data.resize(len);
                }else{
                    tmp_data.assign(wrtie_data.begin(), wrtie_data.end());  
                }
        
                // We already recorded the final offset, so we can just discard the
                // empty buffer now.
                if (tmp_data.empty()) {
                    return tmp_data.size();
                }
        
                size_t write_len = 0;
        
                if (off_len > 0) {
                    if (tmp_data.size() > off_len){
                        std::vector<uint8_t> slice_data(tmp_data.begin(), tmp_data.begin()+off_len);
                        auto first_buf = RangeBuf::from(slice_data, off);
        
                        offset_recv[off] = true;
        
                        data.push_back(first_buf);
                        off += (uint64_t)off_len;
                        length += (uint64_t)off_len;
                        used_length += off_len;
                        write_len += off_len;
                    }else{

                        auto first_buf= RangeBuf::from(tmp_data, off);
                        offset_recv[off] = true;
        
                        data.push_back(first_buf);
                        off += (uint64_t)wrtie_data.size();
                        length += (uint64_t)wrtie_data.size();
                        used_length += wrtie_data.size();
                        write_len += wrtie_data.size();
                        return write_len;
                    }
        
                }
                
                for (auto it = off_len; it < tmp_data.size();){
                    if((tmp_data.size() - it) > SEND_BUFFER_SIZE ){
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.begin() + it + SEND_BUFFER_SIZE);
                        write_len += SEND_BUFFER_SIZE;

                        auto buf = RangeBuf::from(slice_data, off);

                        offset_recv[off] = true;
            
                        // The new data can simply be appended at the end of the send buffer.
                        data.push_back(buf);
            
                        off += (uint64_t)slice_data.size();
                        length += (uint64_t)slice_data.size();
                        used_length += slice_data.size();
                        it += SEND_BUFFER_SIZE;
                    }else{
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.end());
                        write_len += (tmp_data.size() - it);

                        auto buf = RangeBuf::from(slice_data, off);

                        offset_recv[off] = true;
            
                        // The new data can simply be appended at the end of the send buffer.
                        data.push_back(buf);
            
                        off += (uint64_t)slice_data.size();
                        length += (uint64_t)slice_data.size();
                        used_length += slice_data.size();

                        it = tmp_data.size();
                    }
                }
                return write_len;
            }
            else{
                // Get the stream send capacity. This will return an error if the stream
                // was stopped.
                length = (uint64_t)len();
                used_length = len();
                //Addressing left data is greater than the window size
                if (length >= window_size){
                    return 0;
                }
        
                if (wrtie_data.size() == 0){
                    return 0;
                }
            
                // let capacity = self.cap()?;

                std::vector<uint8_t> tmp_data;

                
                if (wrtie_data.size() > capacity) {
                    // Truncate the input buffer according to the stream's capacity.
                    auto copy_len = capacity;
                    std::copy(wrtie_data.begin(), wrtie_data.begin()+copy_len, tmp_data.begin());
                }else{
                    std::copy(wrtie_data.begin(), wrtie_data.end(), tmp_data.begin());

                }
        
                // We already recorded the final offset, so we can just discard the
                // empty buffer now.
                if (tmp_data.empty()) {
                    return data.size();
                }
        
                size_t write_len = 0;

                for (auto it = off_len ; it < tmp_data.size();){
                    if (tmp_data.size() - it > SEND_BUFFER_SIZE){
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.begin() + it + SEND_BUFFER_SIZE);
                        write_len += slice_data.size();
                        auto buf = RangeBuf::from(slice_data, off);
                        offset_recv[off] = true;
                        data.push_back(buf);
                        off += (uint64_t) wrtie_data.size();
                        length += (uint64_t) wrtie_data.size();
                        used_length += wrtie_data.size();
                        it += SEND_BUFFER_SIZE;
                    }else{
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.end());
                        write_len += slice_data.size();
                        auto buf = RangeBuf::from(slice_data, off);
                        offset_recv[off] = true;
                        data.push_back(buf);
                        off += (uint64_t) wrtie_data.size();
                        length += (uint64_t) wrtie_data.size();
                        used_length += wrtie_data.size();
                        it = tmp_data.size();
                    }
                }
                return write_len;
            }
    
        };

        // From application to protocal.
        bool emit(std::vector<uint8_t> &out, size_t& out_len, uint64_t& out_off){
            bool stop = false;
            out_len = out.size();

            out_off = off_front();
        
            while (out_len >= SEND_BUFFER_SIZE && ready())
            {
                if(pos >= data.size()){
                    break;
                }

                auto buf = data.at(pos);

                if (buf->is_empty()) {
                    pos += 1;
                    continue;
                }

                size_t buf_len = 0;
                if (buf->len() > out_len){
                    buf_len = out_len;
                }else{
                    buf_len = buf->len();
                }

                bool partial;
                if(buf_len <= buf->len()){
                    partial = true;
                }else{
                    partial = false;
                }

                // Copy data to the output buffer.
                size_t out_pos = 0;
                std::copy((buf->data).begin(), (buf->data).begin() + buf_len, out.begin() + out_pos);

                length -= (uint64_t)buf_len;
                used_length -= buf_len;

                out_len = buf_len;

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
