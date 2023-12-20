#pragma once

#include <vector>

namespace{

    class RangeBuf{
        std::vector<uint8_t> data,

        size_t start,

        size_t pos,

        size_t len,

        uint64_t off,
    }

    RangeBuf(const std::vector<uint8_t> &buf, size_t len, uint64_t off):
    data(buf),
    start(0),
    pos(0),
    len(len),
    off(off)
    {

    }

    ~RangeBuf(){
        
    }

    static std::shared_ptr<RangeBuf> from(std::vector<uint8_t> buf, uint64_t off){
        return std::make_shared<RangeBuf>(buf, buf.size(), off);
    }

    /// start refers to the start of all data.
    /// Returns the starting offset of `self`.
    /// Lowest offset of data, start == 0, pos == 0
    uint64_t off(){
        return (off - (double)start ) + (double)pos ;
    }

    /// Returns the final offset of `self`.
    /// Returns the largest offset of the data
    uint64_t max_off(){
        return (double)off() + (double)len();
    }

    /// Returns the length of `self`.
    size_t len(){
        // self.len - (self.pos - self.start)
        return (len - pos);
    }

    /// Returns true if `self` has a length of zero bytes.
    bool is_empty(){ 
        return len() == 0;
    }

    /// Consumes the starting `count` bytes of `self`.
    /// Consume() returns the max offset of the current data.
    void consume(size_t count) {
        pos += count;
    }

}
