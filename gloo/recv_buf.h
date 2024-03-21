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

        // void write(std::vector<uint8_t> &out, uint64_t out_off){
        //     auto buf = RangeBuf::from(out, out_off);

        //     size_t buf_len = buf->len();
        //     uint64_t tmp_off = buf->max_off()-(uint64_t)buf_len;

        //     if (!(data.empty())){
        //         auto entry = --data.end();
        //         if (entry -> first == tmp_off){
        //             max_recv_off = tmp_off;
        //         }
        //     }   
        //     data.insert(std::make_pair((buf->off()), buf));
        //     len += buf_len;
        // };

        void write(std::vector<uint8_t> &out, uint64_t out_off){
            auto data_len = data.size();
            if(out_off > data_len){
                data.resize(out_off);
                data.insert(data.end(), 
                            std::make_move_iterator(out.begin()), 
                            std::make_move_iterator(out.end()));
            }
            else if(out_off == data_len){
                data.insert(data.end(), 
                            std::make_move_iterator(out.begin()), 
                            std::make_move_iterator(out.end()));
            }
            else{
                size_t startPos = out_off; 
                size_t endPos = out_off+out.size();  

                auto it = data.erase(data.begin() + startPos, data.begin() + endPos);

                data.insert(it, 
                            std::make_move_iterator(out.begin()), 
                            std::make_move_iterator(out.end()));
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

        // size_t first_item_len(){
        //     auto top_len = 0;
        //     if (!data.empty()){
        //         top_len = data.begin()->second->len();
        //     }
        //     return top_len;
        // }

        // Note: no consideration when recv_buf.size() bigger than given buffer.
        // Reconsider consume function, ant the relationship among off, len, start, write len.⭐️
        // Sendbuf also considers above questions.⭐️
        // Required_len: check the len of first entry in the buffer is same as the required len
        // Output_len: the len of want to pop out from buffer, 0 pop out all data
        // size_t emit(std::vector<uint8_t> &out, size_t output_len = 0){
        //     size_t data_len = 0;
        //     size_t last_max_off = data.begin()->second->max_off();
        //     size_t start_off = data.begin()->second->off();

        //     size_t left = 0;
        //     if (output_len == 0){
        //         left = len;
        //     }else{
        //         left = output_len;
        //     }
            
        //     while (ready() && left > 0){
        //         if (data.empty()){
        //             break;
        //         }

        //         auto entry = data.begin();

        //         // First enty
        //         if (entry->second->off() == start_off){
        //             if (entry->second->len() <= left){
        //                 std::copy((entry->second->data).begin(), (entry->second->data).end(), out.begin());
        //                 data_len += (entry->second)->len();
        //                 left -= (entry->second)->len();
        //                 last_max_off = entry->second->max_off();
        //                 len -= (entry->second)->len();
        //                 data.erase(data.begin());
        //             }else{
        //                 std::copy((entry->second->data).begin(), (entry->second->data).begin()+left, out.begin());
        //                 data_len += left;
        //                 len -= left;
        //                 left = 0;
        //                 (entry->second)->consume(left);
        //             }
        //         }else{
        //             size_t zero = entry->second->off() - last_max_off;
        //             if(zero < left){
        //                 if(zero != 0)
        //                     out.insert(out.end(), zero, 0);
        //                 left -= zero;
        //                 data_len += zero;
        //                 last_max_off += zero;
        //                 auto current_max_off = entry->second->max_off();
        //                 if((current_max_off - last_max_off ) < left){
        //                     std::copy((entry->second->data).begin(), (entry->second->data).end(), out.end());
        //                     // out.insert(out.end(), (entry->second->data).begin(), (entry->second->data).end());
        //                     left -= (entry->second)->len();
        //                     len -= (entry->second)->len();
        //                     data_len += (entry->second)->len();
        //                     last_max_off = entry->second->max_off();
        //                     data.erase(data.begin());
        //                 }else{
        //                     std::copy((entry->second->data).begin(), (entry->second->data).begin()+left, out.end());
        //                     // out.insert(out.end(), (entry->second->data).begin(), (entry->second->data).begin()+left);
        //                     data_len += left;
        //                     len -= left;
        //                     left = 0;
        //                     (entry->second)->consume(left);
        //                 }
        //             }else{
        //                 out.insert(out.end(), zero, 0);
        //                 left -= zero;
        //                 data_len += zero;
        //             }
        //         }

        //     }
        //     return data_len;
        // };

        void reset() {
            data.clear();
            removed = 0;
            len = 0;
        };

        void shutdown()  {
            data.clear();
            len = 0;
        };

        /// Returns the lowest offset of data buffered.
        // double off_front(){
        //     return off;
        // };

        /// Returns true if the stream has data to be read.
        bool ready() {
            return !data.empty();
        };
    };

 

}    