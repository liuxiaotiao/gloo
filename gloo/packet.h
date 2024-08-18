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

using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Priority_len = uint8_t;

using Offset_len = uint32_t;

using Acknowledge_sequence_len = uint64_t;

using Acknowledge_time_len = uint8_t;

using Difference_len = uint8_t;

using Packet_len = uint16_t;

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

        StartAck = 0x08,

        FastAck = 0x09,

        Unknown = 0x10,
    };

// Avoid memory alignment
#pragma pack(push, 1)
    class Header{
        public:
        /// The type of the packet.
        Type ty;

        // Unique for each packet
        Packet_num_len pkt_num;

        // Used to assign retransmission time and other function.
        Priority_len priority;

        // Current packet offset in data flow.
        Offset_len offset;

        // Corresponding to acknowledge packet number. 
        Acknowledge_sequence_len seq;
        
        // To indentify acknowledge packet is partial or all data.
        Acknowledge_time_len ack_time;

        // To differentiate data flow.
        Difference_len difference;

        // The data length of the application packet
        Packet_len pkt_length;


        Header(
            Type first, 
            Packet_num_len pktnum, 
            Priority_len priority, 
            Offset_len off,
            Acknowledge_sequence_len seq,
            Acknowledge_time_len ack_time,
            Difference_len difference,
            Packet_len len) 
            : ty(first), 
            pkt_num(pktnum), 
            priority(priority), 
            offset(off), 
            seq(seq),
            ack_time(ack_time),
            difference(difference),
            pkt_length(len) {};

        ~Header() {};


        void to_bytes(std::vector<uint8_t> &out){
            uint8_t first = 0;
            size_t off = 0;
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
            put_u8(out, first, off); // Type
            
            off += sizeof(uint8_t);
            put_u64(out, pkt_num, off); // packet number

            off += sizeof(Packet_num_len);
            put_u8(out, priority, off); // priority

            off += sizeof(Priority_len);
            put_u32(out, offset, off); // packet offset

            off += sizeof(Offset_len);
            put_u64(out, seq, off); // acknowledge sequence

            off += sizeof(Acknowledge_sequence_len);
            put_u8(out, seq, off); // acknowledge time for one sequcence number.
            
            off += sizeof(Acknowledge_time_len);
            put_u8(out, difference, off); // flow difference

            off += sizeof(Difference_len);
            put_u16(out, pkt_length, off); // packet length

        };

        void put_u64(std::vector<uint8_t> &vec, uint64_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint64_t));
        };

        void put_u32(std::vector<uint8_t> &vec, uint32_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint16_t));
        }

        void put_u16(std::vector<uint8_t> &vec, uint16_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint16_t));
        }

        void put_u8(std::vector<uint8_t> &vec, uint8_t input, size_t position){
            vec.at(position)= input;
        };

        size_t len(){
            return sizeof(Type) + sizeof(Packet_num_len) + sizeof(Priority_len) + sizeof(Offset_len) + sizeof(Acknowledge_sequence_len) + sizeof(Difference_len) + sizeof(pkt_length) + sizeof(Acknowledge_time_len);
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