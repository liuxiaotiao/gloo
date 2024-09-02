#pragma once

#include "gloo/RangeBuf.h"
#include <deque>
#include <map>
#include <vector>
#include <stdlib.h>
namespace dmludp{

     class RecvBuf{
        public:
        // std::map<uint64_t, std::shared_ptr<RangeBuf>> data;
        std::vector<uint8_t> data;

        void * src = nullptr;

        // Used to judge the new coming data stored at data or src;
        bool convert_flag;

        uint64_t off;

        uint64_t len;

        uint64_t last_maxoff;

        uint64_t max_recv_off;

        size_t removed;

        RecvBuf():off(0), len(0), last_maxoff(0), max_recv_off(0), removed(0), convert_flag(false){};

        ~RecvBuf(){};

	    void write(uint8_t* out, size_t out_len, uint64_t out_off, void * target_ = nullptr){
            if (!convert_flag){
                auto data_len = data.size();

                if(out_off > data_len){
                    data.resize(out_off + out_len);
                    memcpy(data.data() + data.size() - out_len, out, out_len * sizeof(uint8_t));
                }
                else if(out_off == data_len){
                    data.resize(out_off + out_len);
                    memcpy(data.data() + data.size() - out_len, out, out_len * sizeof(uint8_t));
                }
                else{
                    size_t startPos = out_off;
                    memcpy(data.data() + startPos, out, out_len * sizeof(uint8_t));
                }
                len += out_len;
                if (len > data.size()){
                    std::cout<<"[Debug] receive buffer len:"<<len<<" vector.size():"<<data.size()<<std::endl;
                    _Exit(0);
                }
            }else{
                auto data_len = last_maxoff;
                if(out_off > data_len){
                    last_maxoff = out_off + out_len;
                    memcpy(src + out_off - 48, out, out_len * sizeof(uint8_t));
                }
                else if(out_off == data_len){
                    last_maxoff = out_off + out_len;
                    memcpy(src + out_off - 48, out, out_len * sizeof(uint8_t));
                }else{
                    memcpy(src + out_off - 48, out, out_len * sizeof(uint8_t));
                }

                len += out_len;
                if (len > last_maxoff){
                    std::cout<<"[Debug] receive len:"<<len<<" last_maxoff:"<<last_maxoff<<std::endl;
                    _Exit(0);
                }
            }
        }

        void get_target(void * target_){
            src = target_;
            convert_flag = true;
            if(data.size() > 48){
                memcpy(src, data.data() + 48, data.size() - 48);
                data.resize(48);
            }
        }

        size_t receive_length(){
            return len;
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
            if (!convert_flag){
                if ((output_len + removed) > data.size()){
                    return emitLen;
                }

                if (removed == data.size()){
                    return emitLen;
                }

                memcpy(out, data.data() + removed, output_len);
                convert_flag = false;
                emitLen = output_len;
                removed += output_len;
            }else{
                emitLen = last_maxoff - 48;
            }
            return emitLen;
        }

        void reset() {
            data.resize(0);
            removed = 0;
            len = 0;
            src = nullptr;
            last_maxoff = 0;
            convert_flag = false;
        };

        void shutdown()  {
            data.resize(0);
            len = 0;
        };

        /// Returns true if the stream has data to be read.
        bool ready() {
            return !data.empty();
        };
    };
}    