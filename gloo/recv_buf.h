#pragma once

#include <vector>
#include <stdlib.h>
namespace dmludp{

    class RecvBuf{
        public:
        std::vector<uint8_t> data;

        void * src = nullptr;

        // Used to judge the new coming data stored at data or src;
        bool convert_flag;

        uint64_t off;

        uint64_t len;

        size_t removed;

        RecvBuf():off(0), len(0), removed(0), convert_flag(false), data(10, 0){};

        ~RecvBuf(){};

        void reg(size_t out_len){
            len += out_len;
        }

	    void write(uint8_t* out, size_t out_len, uint64_t out_off){
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
                // size_t endPos = out_off + out_len;
                memcpy(data.data() + startPos, out, out_len * sizeof(uint8_t));
            }
            // len += out_len;
    	    // std::cout<<"[Debug] receive buffer len:"<<len<<" vector.size():"<<data.size()<<std::endl;
	        if (len > data.size()){
				std::cout<<"[Debug] receive buffer len:"<<len<<" vector.size():"<<data.size()<<std::endl;
                _Exit(0);
            }
        }

        size_t receive_length(){
            return len;
        }

        void get_target(void * target_){
            src = target_;
            convert_flag = true;
            if(data.size() > 48){
                memcpy(src, data.data() + 48, data.size() - 48);
                data.resize(48);
            }
        }

        bool is_empty(){
            return data.empty();
        }

        size_t length(){
            return (data.size() - removed);
        }

        /* NOT USE*/
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
                    convert_flag = false;
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
                convert_flag = false;
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
