#include <deque>
#include <RangeBuf.h>
#include <algorithm>

namespace dmludp{
    class SendBuf{
        public:
        std::deque<std::shared_ptr<RangeBuf>> data;

        size_t pos;

        uint64_t off;

        uint64_t len;

        uint64_t len;

        uint64_t max_data;

        // error

        size_t used_length;

        std::map<uint64_t, bool> offset_recv;

        uint64_t removed;

        size_t sent;

        size_t cap(){
            used_length = len();
            return (size_t)(max_data - (uint64_t)used_length as u64);
        }

        /// Returns the lowest offset of data buffered.
        // uint64_t off_front() {
        //     auto tmp_pos = pos;

        //     // Skip empty buffers from the start of the queue.
        //     while let Some(b) = self.data.get(pos) {
        //         if !b.is_empty() {
        //             return b.off();
        //         }
        //         pos += 1;
        //     }

        //     self.off
        // }

        /// Returns true if there is data to be written.
        bool ready(){
            return !data.empty();
        }


        void clear(){
            data.clear();
            pos = 0;
            off = 0;
            len = 0;
        }

        /// Returns the largest offset of data buffered.
        uint64_t off_back(){
            return off;
        }

        /// The maximum offset we are allowed to send to the peer.
        uint64_t max_off() {
            return max_data;
        }

        bool is_empty(){
            return data.empty();
        }

        size_t len(){
            size_t length = 0;
            if data.empty(){
                return 0;
            }

            std::for_each(data.begin(), data.end(), [](int x) {
                length += (x->data).size();
            });
            return length;
        }
        /// Returns true if the stream was stopped before completion.
        // bool is_stopped(){
        //     self.error.is_some()
        // }

        /// Updates the max_data limit to the given value.
        void update_max_data(uint64_t maxdata) {
            max_data = maxdata;
        }

        ////rewritetv
        /// Resets the stream at the current offset and clears all buffered data.
        uint64_t reset(){
            auto unsent_off = off_front();
            auto unsent_len = off_back() - unsent_off;

            // Drop all buffered data.
            data.clear();

            pos = 0;
            len = 0;
            off = unsent_off;

            return unsent_len;
        }

        // Mark received data as false.
        void ack_and_drop(uint64_t offset){
            auto it = offset_recv.find(offset);
            if (it != offset_recv.end()) {
                it->second = false;
            }
        }

        // pub fn recv_and_drop(&mut self, ack_set: &[u64], unack_set: &[u64],len: usize) {
        // Remove received data from send buffer.
        void recv_and_drop() {
            if data.empty() {
                return;
            }
            size_t currentIndex = 0; 
            for (auto it = offset_recv.begin(); it != offset_recv.end(); /* no increment here */){
                if (it->second == true){
                    data[currentIndex++] = data[it->first];
                    it = myoffset_recvMap.erase(it);
                }else{
                    ++it;
                }
            }
        }

        size_t write(std::vector<uint8_t> data, size_t window_size, size_t off_len, uint64_t _max_ack) {
            // All data in the buffer has been sent out, remove received data from the buffer.
            if (pos == 0) {
                recv_and_drop();
            }

            auto capacity = cap();
            if capacity == 0 {
                return 0;
            }

            if (is_empty()){
                max_data = (uint64_t)window_size;
                removed = 0;
                sent = 0;
                // Get the send capacity. This will return an error if the stream
                // was stopped.
                len = (uint64_t)len();
                used_length = len();
                //Addressing left data is greater than the window size
                if (len >= window_size){
                    return 0;
                }
        
                if (data.len() == 0){
                    return 0;
                }
                
                std::vector<uint8_t> tmp_data;

                if (data.size() > capacity) {
                    // Truncate the input buffer according to the stream's capacity.
                    auto len = capacity;
                    std::copy(data.begin(), data.begin()+len, tmp_data.begin());
                    // data = data.resize(len);
                }else{
                    std::copy(data.begin(), data.end(), tmp_data.begin());
                }
        
                // We already recorded the final offset, so we can just discard the
                // empty buffer now.
                if (tmp_data.empty()) {
                    return tmp_data.size();
                }
        
                size_t len = 0;
        
                if (off_len > 0) {
                    if (tmp_data.size() > off_len){
                        std::vector<uint8_t> slice_data(tmp_data.begin(), tmp_data.begin()+off_len);
                        auto first_buf = RangeBuf::from(slice_data, off);
        
                        offset_recv[off] = true;
        
                        data.push_back(first_buf);
                        off += (uint64_t)off_len;
                        len += (uint64_t)off_len;
                        used_length += off_len;
                    l   en += off_len;
                    }else{

                        auto first_buf= RangeBuf::from(tmp_data, off);
                        offset_recv[off] = true;
        
                        data.push_back(first_buf);
                        off += (uint64_t)data.size();
                        len += (uint64_t)data.size();
                        used_length += data.size();
                        len += data.size();
                        return len;
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
                        len += (uint64_t)slice_data.size();
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
                        len += (uint64_t)slice_data.size();
                        used_length += slice_data.size();

                        it = tmp_data.size();
                    }
                }
                return write_len;
            }
            else{
                // Get the stream send capacity. This will return an error if the stream
                // was stopped.
                len = (uint64_t)len();
                used_length = len();
                //Addressing left data is greater than the window size
                if (len >= window_size){
                    return 0;
                }
        
                if (data.size() == 0){
                    return 0;
                }
            
                // let capacity = self.cap()?;

                std::vector<uint8_t> tmp_data;

                
                if data.size() > capacity {
                    // Truncate the input buffer according to the stream's capacity.
                    auto len = capacity;
                    data = &data[..len];
                    std::copy(data.begin(), data.begin()+len, tmp_data.begin());
                }else{
                    std::copy(data.begin(), data.end(), tmp_data.begin());

                }
        
                // We already recorded the final offset, so we can just discard the
                // empty buffer now.
                if (tmp_data.empty()) {
                    return data.size();
                }
        
                size_t len = 0;

                for (auto it = off_len ; it < tmp_data.size();){
                    if (tmp_data.size() - it > SEND_BUFFER_SIZE){
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.begin() + it + SEND_BUFFER_SIZE);
                        write_len += slice_data.size();
                        auto buf = RangeBuf::from(slice_data, off);
                        offset_recv[off] = true;
                        data.push_back(buf);
                        off += (uint64_t) data.size();
                        len += (uint64_t) data.size();
                        used_length += data.size();
                        it += SEND_BUFFER_SIZE;
                    }else{
                        std::vector<uint8_t> slice_data(tmp_data.begin()+it, tmp_data.end());
                        write_len += slice_data.size();
                        auto buf = RangeBuf::from(slice_data, off);
                        offset_recv[off] = true;
                        data.push_back(buf);
                        off += (uint64_t) data.size();
                        len += (uint64_t) data.size();
                        used_length += data.size();
                        it = tmp_data.size();
                    }
                }
                return write_len;
            }
    
        }

        bool emit(std::vector<uint8_t> out, size_t& out_len, uint64_t& out_off){
            bool stop = false;
            size_t out_len = out.size();

            out_off = off_front();
        
            while (out_len >= SEND_BUFFER_SIZE && ready())
            {
                let buf = match self.data.get_mut(self.pos) {
                    Some(v) => v,

                    None => break,
                };
                if(pos > data.size()){
                    break;
                }

                auto buf = data.at(pos);

                if (buf->empty()) {
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

                len -= (uint64_t)buf_len;
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
        }

    

    }
    
}
