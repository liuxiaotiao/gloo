#pragma once

#include <cmath>
#include <set>
#include "gloo/connection.h"
namespace dmludp{

// Congestion Control
const size_t INITIAL_WINDOW_PACKETS = 8;

const size_t ELICT_ACK_CONSTANT = 8;

const size_t MINIMUM_WINDOW_PACKETS = 2;

const size_t INI_WIN = 1350 * 8;

// const size_t PACKET_SIZE = 1200;
const size_t PACKET_SIZE = 1350;

enum CongestionControlAlgorithm {
    /// CUBIC congestion control algorithm (default). `cubic` in a string form.
    NEWCUBIC = 1,

};

class RecoveryConfig {
    public:
    size_t max_send_udp_payload_size;

    RecoveryConfig(){

    };

    ~RecoveryConfig(){

    };

};

class Recovery{
    public:

    bool app_limit;

    size_t congestion_window;

    size_t bytes_in_flight;

    size_t max_datagram_size;
    // k:f64,
    size_t incre_win;

    size_t decre_win;

    std::set<size_t> former_win_vecter;

    bool roll_back_flag;

    bool function_change;

    size_t incre_win_copy;

    size_t decre_win_copy;

    Recovery():
    app_limit(false),
    // congestion_window(INI_WIN),
    bytes_in_flight(0),
    // max_datagram_size(PACKET_SIZE);
    incre_win(0),
    decre_win(0),
    roll_back_flag(false),
    function_change(false),
    incre_win_copy(0),
    decre_win_copy(0){
        max_datagram_size = PACKET_SIZE;
        congestion_window = INI_WIN;
    };

    ~Recovery(){};

    void on_init() {
        congestion_window = max_datagram_size * INITIAL_WINDOW_PACKETS;
    };

    void reset() {
        congestion_window = max_datagram_size * INITIAL_WINDOW_PACKETS;
    };

    // cwnd = C(-(P - K))^3/k^3 + Wmax
    //x = [0:0.1:3.6];
    //y = -8*(x-1.8).^3/1.8^3;
    void update_win(float weights, double num){
        float winadd_copy = 0;
        double winadd = 0;
        if(function_change){
            winadd = (3 * pow((double)weights, 2) - 12 * (double)weights + 4) * (double)max_datagram_size;
        }else{
            if (weights > 0){
                function_change = true;
                incre_win = incre_win_copy;
                decre_win = decre_win_copy;
            }
            winadd_copy = (3 * pow((double)weights, 2)  - 12 *(double)weights + 4) * (double)max_datagram_size; 
            winadd = num * (double)max_datagram_size;
        }

        if (winadd != num){
            roll_back_flag = true;
        }

        if (winadd > 0){
            incre_win += (size_t)winadd;
        }else {
            decre_win += (size_t)(-winadd);
        }

        if (winadd_copy > 0){
            incre_win_copy += (size_t)winadd_copy;
        }else {
            decre_win_copy += (size_t)(-winadd_copy);
        }
    };


    /// Returns whether or not we should elicit an ACK even if we wouldn't
    /// otherwise have constructed an ACK eliciting packet.
    bool should_elicit_ack(size_t pkt_num) {
        if (pkt_num % ELICT_ACK_CONSTANT == 0){
            return true;
        }else{
            return false;
        }
    };

    ///modified
    size_t cwnd(){
        size_t tmp_win = 0;
        if (2*incre_win > decre_win){
            tmp_win = 2*incre_win - decre_win;
        }else{
            tmp_win = 0;
        }
        if (!roll_back_flag) {
            former_win_vecter.insert(tmp_win);
        }

        congestion_window = tmp_win;
        incre_win = 0;
        decre_win = 0;
        incre_win_copy = 0;
        decre_win_copy = 0;
        if (congestion_window >=  PACKET_SIZE*INITIAL_WINDOW_PACKETS){
            return congestion_window;
        }else{
            congestion_window = PACKET_SIZE*INITIAL_WINDOW_PACKETS;
            return congestion_window;
        }
        
    };
    

    size_t cwnd_available()  {
        return (congestion_window - bytes_in_flight);
    };

    void collapse_cwnd() {
        congestion_window = INI_WIN;
    };

    void update_app_limited(bool v) {
        app_limit = v;
    };

    bool app_limited(){
        return app_limit;
    };
    
    size_t rollback(){
        if (former_win_vecter.empty()){
            congestion_window = INI_WIN;
        }else{
            congestion_window = *former_win_vecter.rbegin();
            former_win_vecter.erase(--former_win_vecter.end()); 
        }
        return congestion_window;
    };

};

}