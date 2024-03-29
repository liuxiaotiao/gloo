#pragma once
#include <iostream>
#include <vector>
#include <cstdint>
#include <iostream>
#include <vector>
#include <cstdint>
#include <functional>
#include <unordered_map>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define IS_BIG_ENDIAN 1
#else
    #define IS_BIG_ENDIAN 0
#endif

namespace dmludp{
    enum Type : uint8_t {
        /// Retry packet.
        Retry = 0x01,

        /// Handshake packet.
        Handshake = 0x02,

        /// application packet.
        Application = 0x03,

        /// server ask reciver
        ElicitAck = 0x04,

        ///ACK
        ACK = 0x05,

        /// STOP
        Stop = 0x06,

        /// Fin
        Fin = 0x07,

        // StartACK
        StartAck = 0x08,

        Unknown = 0x09,
    };

// Avoid memory alignment
#pragma pack(push, 1)
    class Header{
        public:
        /// The type of the packet.
        Type ty;

        uint64_t pkt_num;

        uint8_t priority;
        ///This offset is different from TCP offset. It refers to the last position in the 
        uint64_t offset;

        uint64_t pkt_length;


        Header(
            Type first, 
            uint64_t pktnum, 
            uint8_t priority, 
            uint64_t off, 
            uint64_t len) 
            : ty(first), 
            pkt_num(pktnum), 
            priority(priority), 
            offset(off), 
            pkt_length(len) {};

        ~Header() {};


        static std::shared_ptr<Header> from_slice(std::vector<uint8_t> b){
            int off = 0;
            auto first = get_u8(b, off);
            off += sizeof(uint8_t);

            Type ty;
            if (first == 0x01) {
                ty = Type::Retry;
            }else if (first == 0x02){
                ty = Type::Handshake;
            }else if (first == 0x03){
                ty = Type::Application;
            }else if (first == 0x04){
                ty = Type::ElicitAck;
            }else if (first == 0x05){
                ty = Type::ACK;
            }else if (first == 0x06){
                ty = Type::Stop;
            }else if (first == 0x07){
                ty = Type::Fin;
            }else if (first = 0x08){
                ty = Type::StartAck;
            }else{
                ty = Type::Unknown;
            }

            uint64_t second = get_u64(b, off);
            off += sizeof(uint64_t);
            uint8_t third = get_u8(b, off);
            off += sizeof(uint8_t);
            uint64_t forth = get_u64(b, off);
            off += sizeof(uint64_t);
            uint64_t fifth = get_u64(b, off);

            return std::make_shared<Header>(ty, second, third, forth, fifth);
        };

        static std::shared_ptr<Header> from_bytes(std::vector<uint8_t> &b){
            int off = 0;
            auto first = get_u8(b, off);
            Type ty;
            if (first == 0x01) {
                ty = Type::Retry;
            }else if (first == 0x02){
                ty = Type::Handshake;
            }else if (first == 0x03){
                ty = Type::Application;
            }else if (first == 0x04){
                ty = Type::ElicitAck;
            }else if (first == 0x05){
                ty = Type::ACK;
            }else if (first == 0x06){
                ty = Type::Stop;
            }else if (first == 0x07){
                ty = Type::Fin;
            }else if (first = 0x08){
                ty = Type::StartAck;
            }else{
                ty = Type::Unknown;
            }
            uint64_t second = *reinterpret_cast<const uint64_t*>(b.data() + 1);
            uint8_t third = b[9];
            uint64_t forth = *reinterpret_cast<const uint64_t*>(b.data() + 10);
            uint64_t fifth = *reinterpret_cast<const uint64_t*>(b.data() + 18);

            return std::make_shared<Header>(ty, second, third, forth, fifth);
        };

        void to_bytes(std::vector<uint8_t> &out){
            uint8_t first = 0;
            int off = 0;
            if (ty == Type::Retry){
                first = 0x01;
            }else if (ty == Type::Handshake){
                first = 0x02;
            }else if (ty == Type::Application){
                first = 0x03;
            }else if (ty == Type::ElicitAck){
                first = 0x04;
            }else if (ty == Type::ACK){
                first = 0x05;
            }else if (ty == Type::Stop){
                first = 0x06;
            }else if (ty == Type::Fin){
                first = 0x07;
            }else if (ty == Type::StartAck){
                first = 0x08;
            }else{
                first = 0x09;
            }

            put_u8(out, first, off);
            
            off += sizeof(uint8_t);
            put_u64(out, pkt_num, off);
            off += sizeof(uint64_t);

            put_u8(out, priority, off);
            off += sizeof(uint8_t);
            put_u64(out, offset, off);
            off += sizeof(uint64_t);
            put_u64(out, pkt_length, off);

        };

        static uint64_t get_u64(std::vector<uint8_t> vec, int start){
            uint64_t value = 0;
            std::vector data_slice(vec.begin() + start, vec.begin() + start + sizeof(uint64_t));
            #if IS_BIG_ENDIAN
                for (int i = 0; i < 8; ++i) {
                    value |= static_cast<uint64_t>(data_slice[i]) << ((7 - i) * 8);
                }
            #else
                for (int i = 0; i < 8; ++i) {
                    value |= static_cast<uint64_t>(data_slice[i]) << (i * 8);
                }
            #endif
            return value;
        };

        static uint8_t get_u8(std::vector<uint8_t> vec, int position){
            return vec[position];
        };

        void put_u64(std::vector<uint8_t> &vec, uint64_t input, int position){
            std::vector<uint8_t> data_slice(sizeof(uint64_t));
            #if IS_BIG_ENDIAN
                for (int i = 0; i < sizeof(uint64_t); ++i) {
                    data_slice[i] = static_cast<uint8_t>(input >> ((7 - i) * 8));
                }
            #else 
                for (int i = 0; i < sizeof(uint64_t); ++i) {
                    data_slice[i] = static_cast<uint8_t>(input >> (i * 8));
                }
            #endif
            std::copy(data_slice.begin(), data_slice.end(), vec.begin() + position);
        };

        void put_u8(std::vector<uint8_t> &vec, uint8_t input, int position){
            vec.at(position)= input;
        };

        size_t len(){
            return 26;
        };
    };

    class PktNumSpace{
        public:

        uint64_t next_pkt_num;

        std::unordered_map<uint64_t, uint64_t > priority_record;

        std::unordered_map<uint64_t, std::array<uint64_t, 2>> record;

        PktNumSpace():next_pkt_num(0){};

        ~PktNumSpace(){};

        uint64_t updatepktnum(){
            next_pkt_num += 1;
            return (next_pkt_num - 1);
        };

        void reset(){
            next_pkt_num = 0;
        };
    };
}
#pragma pack(pop)

