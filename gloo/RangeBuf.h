#pragma once

#include <vector>
#include <memory>
namespace dmludp{
    class RangeBuf final : public std::enable_shared_from_this<RangeBuf>{
        public:
        std::vector<uint8_t> data;
        // 
        private:
        size_t start;
        // Used data position.
        size_t pos;
        // Total length of the left data.
        size_t length;
        // Start offset
        uint64_t offset;

        public:
        RangeBuf(const std::vector<uint8_t> &buf, size_t len, uint64_t off):
        data(buf),
        start(0),
        pos(0),
        length(len),
        offset(off)
        {};

        ~RangeBuf(){};

        static std::shared_ptr<RangeBuf> from(const std::vector<uint8_t> &buf, uint64_t off){
            return std::make_shared<RangeBuf>(buf, buf.size(), off);
        };

        /// start refers to the start of all data.
        /// Returns the starting offset of `self`.
        /// Lowest offset of data, start == 0, pos == 0
        uint64_t off(){
            return ((offset - (uint64_t)start ) + (uint64_t)pos );
        };

        /// Returns the final offset of `self`.
        /// Returns the largest offset of the data
        uint64_t max_off(){
            return (off() + (uint64_t)(len()));
        };

        /// Returns the length of `self`.
        size_t len(){
            // self.len - (self.pos - self.start)
            return (length - pos);
        };

        /// Returns true if `self` has a length of zero bytes.
        bool is_empty(){ 
            return (len() == 0);
        };

        /// Consumes the starting `count` bytes of `self`.
        /// Consume() returns the max offset of the current data.
        void consume(size_t count) {
            pos += count;
        };
    };
}
