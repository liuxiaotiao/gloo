#pragma once

#include "gloo/RangeBuf.h"
#include <deque>
#include <map>
#include <vector>

namespace dmludp{

    class RecvBuf{
        public:
        // std::map<uint64_t, std::shared_ptr<RangeBuf>> data;
        std::vector<uint8_t> data;

        uint64_t off;

        uint64_t len;

        uint64_t last_maxoff;

        uint64_t max_recv_off;

        size_t removed;

        RecvBuf():off(0), len(0), last_maxoff(0), max_recv_off(0), removed(0){};

        ~RecvBuf(){};

        void write(std::vector<uint8_t> &out, uint64_t out_off){
            auto data_len = data.size();
            if(out_off > data_len){
                // data.resize(out_off);
                // data.insert(data.end(), 
                //             std::make_move_iterator(out.begin()), 
                //             std::make_move_iterator(out.end()));
                data.resize(out_off + out.size());
                memcpy(data.data() + data.size() - out.size(), out.data(), out.size() * sizeof(uint8_t));
            }
            else if(out_off == data_len){
                // data.insert(data.end(), 
                //             std::make_move_iterator(out.begin()), 
                //             std::make_move_iterator(out.end()));
                data.resize(out_off + out.size());
                memcpy(data.data() + data.size() - out.size(), out.data(), out.size() * sizeof(uint8_t));
            }
            else{
                size_t startPos = out_off; 
                size_t endPos = out_off+out.size();  

                // auto it = data.erase(data.begin() + startPos, data.begin() + endPos);

                // data.insert(it, 
                //             std::make_move_iterator(out.begin()), 
                //             std::make_move_iterator(out.end()));
                memcpy(data.data() + startPos, out.data(), out.size() * sizeof(uint8_t));
            }
            len += out.size();
        }


        uint64_t max_ack(){
            return max_recv_off;
        }

        bool is_empty(){
            return data.empty();
        }

        size_t length(){
            return (data.size() - removed);
        }

        size_t first_item_len(size_t checkLength){
            size_t checkresult = 0;
            if (data.size() >= checkLength) {
                bool allZeros = std::all_of(data.begin(), data.begin() + checkLength,
                                                [](uint8_t val) { return val == 0; });
                if(!allZeros) {
                    checkresult = checkLength;
                }
            }
            return checkresult;
        }

        void data_padding(size_t paddingLength){
            if (data.size() < (removed + paddingLength)){
                data.resize(removed + paddingLength);
            }
        }

        // when output_len is 0, left data will be emiited.
        size_t emit(uint8_t* out, bool iscopy, size_t output_len = 0){
            size_t emitLen = 0;
            if (iscopy){
                if (output_len == 0){
                    // out = static_cast<uint8_t*>(data.data() + removed);
                    memcpy(out, data.data() + removed, data.size());
                    emitLen = data.size() - removed;
                    removed = data.size();
                    return emitLen;
                }

                if ((output_len + removed) > data.size()){
                    return emitLen;
                }

                if (removed == data.size()){
                    return emitLen;
                }

                memcpy(out, data.data() + removed, output_len);
                emitLen = output_len;
                removed += output_len;
            }else{
                if (output_len == 0){
                    out = static_cast<uint8_t*>(data.data() + removed);
                    emitLen = data.size() - removed;
                    removed = data.size();
                    return emitLen;
                }

                if ((output_len + removed) > data.size()){
                    return emitLen;
                }

                if (removed == data.size()){
                    return emitLen;
                }

                out = static_cast<uint8_t*>(data.data() + removed);
                emitLen = output_len;
                removed += output_len;
            }
            return emitLen;
        }

        void reset() {
            data.clear();
            removed = 0;
            len = 0;
        };

        void shutdown()  {
            data.clear();
            len = 0;
        };

        /// Returns true if the stream has data to be read.
        bool ready() {
            return !data.empty();
        };
    };

 

}    