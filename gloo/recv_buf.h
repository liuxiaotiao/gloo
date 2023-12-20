#pragma once

#include <RangeBuf.h>
#include <deque>
#include <map>

namespace dmludp{

    class RecvBuf{
        public:
        std::map<uint64_t, std::shared_ptr(RangeBuf)> data;

        uint64_t off,

        uint64_t len,

        uint64_t last_maxoff,

        uint64_t max_recv_off,
    }

    RecvBuf::RecvBuf(){}

    ~RecvBuf()

    void write(std::vector<uint8_t> out, uint64_t out_off){
        auto buf = RangeBuf::from(out, out_off);

        size_t buf_len = buf.len();
        uint64_t tmp_off = buf.max_off()-(uint64_t)buf_len;

        if (!data.empty()){
            auto entry = data.end();
            

            let (k,_v) = self.data.last_key_value().unwrap();
            if *k == tmp_off{
                max_recv_off = tmp_off;
            }
        }   
        self.data.insert(buf.max_off(), buf);
    }

    // Note: no consideration when recv_buf.size() bigger than given buffer.
    size_t emit(std::vector<uint8_t> out){
        size_t len = 0;
        size_t current_off = data.begin()->second->off();
        
        while (ready()){
            if (data.empty()){
                breakl
            }
            auto entry = data.begin();
            auto zero = entry->second->off() - current_off - data.size();
            if (zero > 0 ){
                out.insert(out.end(), zero, 0);
            }
            out.insert(out.end(), ((entry->second)->data).begin(), ((entry->second)->data).end());
            len += (zero + (entry->second)->len());

            data.erase(data.begin());

        }
        return len;
    }

    size_t reset(double final_size) {

    }

    void shutdown()  {
        data.clear();
    }

    /// Returns the lowest offset of data buffered.
    double off_front(){
        return off;
    }

    /// Returns true if the stream has data to be read.
    bool ready() {
        return !data.is_empty();
    }

}    