#pragma once

#include "gloo/RangeBuf.h"
#include <deque>
#include <map>
#include <vector>

namespace dmludp{

    class RecvBuf{
        public:
        std::map<uint64_t, std::shared_ptr<RangeBuf>> data;

        uint64_t off;

        uint64_t len;

        uint64_t last_maxoff;

        uint64_t max_recv_off;

        RecvBuf():off(0), len(0), last_maxoff(0), max_recv_off(0){};

        ~RecvBuf(){};

        void write(std::vector<uint8_t> &out, uint64_t out_off){
            auto buf = RangeBuf::from(out, out_off);

            size_t buf_len = buf->len();
            uint64_t tmp_off = buf->max_off()-(uint64_t)buf_len;

            if (!(data.empty())){
                auto entry = --data.end();
                if (entry -> first == tmp_off){
                    max_recv_off = tmp_off;
                }
            }   
            data.insert(std::make_pair((buf->off()), buf));
            len += buf_len;
        };

        uint64_t max_ack(){
            return max_recv_off;
        }

        bool is_empty(){
            return data.empty();
        }

        size_t length(){
            return (size_t)len;
        }

        size_t first_item_len(){
            auto top_len = 0;
            if (!data.empty()){
                top_len = data.begin()->second->len();
            }
            return top_len;
        }

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

        // new version delete
        size_t emit(std::vector<uint8_t> &out, size_t output_len = 0){
            size_t data_len = 0;
            size_t last_max_off = data.begin()->second->max_off();
            size_t start_off = data.begin()->second->off();

            size_t left = 0;
            if (output_len == 0){
                left = len;
            }else{
                left = output_len;
            }
            
            auto itEnd = 0;
            auto entry = data.begin();
            while (ready() && left > 0){
                if (data.empty()){
                    break;
                }

                // auto entry = data.begin();
                itEnd = entry->first;
                // First enty
                if (entry->second->off() == start_off){
                    if (entry->second->len() <= left){
                        std::copy((entry->second->data).begin(), (entry->second->data).end(), out.begin());
                        data_len += (entry->second)->len();
                        left -= (entry->second)->len();
                        last_max_off = entry->second->max_off();
                        len -= (entry->second)->len();
                        // data.erase(data.begin());
                    }else{
                        std::copy((entry->second->data).begin(), (entry->second->data).begin()+left, out.begin());
                        data_len += left;
                        len -= left;
                        left = 0;
                        (entry->second)->consume(left);
                    }
                }else{
                    size_t zero = entry->second->off() - last_max_off;
                    if(zero < left){
                        if(zero != 0)
                            out.insert(out.end(), zero, 0);
                        left -= zero;
                        data_len += zero;
                        last_max_off += zero;
                        auto current_max_off = entry->second->max_off();
                        if((current_max_off - last_max_off ) < left){
                            std::copy((entry->second->data).begin(), (entry->second->data).end(), out.end());
                            left -= (entry->second)->len();
                            len -= (entry->second)->len();
                            data_len += (entry->second)->len();
                            last_max_off = entry->second->max_off();
                            // data.erase(data.begin());
                        }else{
                            std::copy((entry->second->data).begin(), (entry->second->data).begin()+left, out.end());
                            data_len += left;
                            len -= left;
                            left = 0;
                            (entry->second)->consume(left);
                        }
                    }else{
                        out.insert(out.end(), zero, 0);
                        left -= zero;
                        data_len += zero;
                    }
                }
                entry++;

            }
            auto Endit = data.find(itEnd);
            if (Endit != data.end()) {
                Endit++; 
            }
            data.erase(data.begin(), Endit);
            return data_len;
        };

        size_t reset(double final_size) {
            return 0;
        };

        void shutdown()  {
            data.clear();
            len = 0;
        };

        /// Returns the lowest offset of data buffered.
        double off_front(){
            return off;
        };

        /// Returns true if the stream has data to be read.
        bool ready() {
            return !data.empty();
        };
    };

 

}    