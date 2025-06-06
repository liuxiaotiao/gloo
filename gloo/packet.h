#pragma once
#include <iostream>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

namespace dmludp{

using Type_len = uint8_t;

using Packet_num_len = uint64_t;

using Offset_len = uint64_t;

using Difference_len = uint32_t;

using Packet_len = uint16_t;

using Priority_len = uint8_t;

using Status_len = uint8_t;

using Importance_len = uint16_t;

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

        // Current packet offset in data flow.
        Offset_len offset;

        /* difference start from 0*/
        Difference_len difference;

        // The data length of the application packet
        Packet_len pkt_length;

        /*
        Importance pkt_importance;

        Status pkt_status;
        */


        Header(
            Type first = Type::Application, 
            Packet_num_len pktnum = 0, 
            Offset_len off = 0,
            Difference_len difference = 0,
            Packet_len len = 0) 
            : ty(first), 
            pkt_num(pktnum), 
            offset(off), 
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

            off += sizeof(Packet_len);
            put_u64(out, offset, off); // packet offset
            
            off += sizeof(Offset_len);
            put_u32(out, difference, off); // flow difference

            off += sizeof(Difference_len);
            put_u16(out, pkt_length, off); // packet length

        };

        void put_u64(std::vector<uint8_t> &vec, uint64_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint64_t));
        };

        void put_u32(std::vector<uint8_t> &vec, uint32_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint32_t));
        }

        void put_u16(std::vector<uint8_t> &vec, uint16_t &input, size_t position){
            memcpy(vec.data() + position, &input, sizeof(uint16_t));
        }

        void put_u8(std::vector<uint8_t> &vec, uint8_t input, size_t position){
            vec.at(position)= input;
        };

        void set_ty(Type ty_){
            ty = ty_;
        }

        Type get_ty(){
            return ty;
        }

        void set_pkt_num(Packet_num_len pn_){
            pkt_num = pn_;
        }

        Packet_num_len get_pkt_num(){
            return pkt_num;
        }

        void set_offset(Offset_len offset_){
            offset = offset_;
        }

        Offset_len get_offset(){
            return offset;
        } 

        void set_difference(Difference_len difference_){
            difference = difference_;
        }

        Difference_len get_difference(){
            return difference;
        }

        void set_pkt_length(Packet_len length_){
            pkt_length = length_;
        }

        Packet_len get_pkt_length(){
            return pkt_length;
        }

        static size_t len(){
            return sizeof(Type) 
                + sizeof(Packet_num_len) 
                + sizeof(Offset_len) 
                + sizeof(Difference_len) 
                + sizeof(pkt_length);
        };
    };

    class PktNumSpace{
        public:

        Packet_num_len next_pkt_num;

        PktNumSpace():next_pkt_num(0){};

        ~PktNumSpace(){};

        Packet_num_len updatepktnum(){
            next_pkt_num += 1;
            return (next_pkt_num - 1);
        };

        void reset(){
            next_pkt_num = 0;
        };

        Packet_num_len getpktnum(){
            return (next_pkt_num - 1);
        }

        Packet_num_len expectpktnum(){
            return next_pkt_num;
        }
    };
}
#pragma pack(pop)

