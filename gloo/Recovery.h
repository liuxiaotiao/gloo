#pragma once

#include <cmath>
#include <set>
#include "gloo/connection.h"

namespace dmludp{
const size_t INITIAL_WINDOW_PACKETS = 2;

const size_t PACKET_SIZE =8900;

const size_t INI_WIN = PACKET_SIZE * INITIAL_WINDOW_PACKETS;

const size_t INI_SSTHREAD = PACKET_SIZE * 40;

const double BETA = 0.7;

const double C = 0.4;

enum CongestionControlAlgorithm {
    /// CUBIC congestion control algorithm (default). `cubic` in a string form.
    NEWCUBIC = 1,

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

    size_t last_cwnd;

    size_t cubic_time;

    size_t W_max;

    size_t W_last_max;

    bool is_congestion;

    size_t cwnd_increment;

    size_t ssthread;

    bool is_slow_start;

    bool no_loss;

    size_t parital_allowed;

    bool timeout_recovery;

    double K;

    Recovery():
    app_limit(false),
    bytes_in_flight(0),
    incre_win(0),
    decre_win(0),
    roll_back_flag(false),
    function_change(false),
    incre_win_copy(0),
    decre_win_copy(0),
    is_congestion(false),
    cubic_time(0),
    is_slow_start(true),
    no_loss(true),
    parital_allowed(INI_WIN),
    timeout_recovery(false),
    K(0.0),
    ssthread(INI_SSTHREAD){
        max_datagram_size = PACKET_SIZE;
        congestion_window = 0;
        last_cwnd = INI_WIN;
        W_max = INI_WIN;
        W_last_max = INI_WIN;
    };

    ~Recovery(){};


    void change_status(bool condition_flag){
        is_congestion = condition_flag;
        is_slow_start = !condition_flag;
        W_max = congestion_window;
    }

    void reset() {
        congestion_window = max_datagram_size * INITIAL_WINDOW_PACKETS;
    };

    void update_win(bool update_cwnd, size_t instant_send = 0, bool timeout_ = false){
        if (update_cwnd){
            // highest priority
            if (instant_send){
                no_loss = true;
                timeout_recovery = false;
            }else{
                no_loss = false;
                timeout_recovery = false;
            }
        }else{
            if (!timeout_){
                parital_allowed = instant_send;
                no_loss = true;
                timeout_recovery = true;
            }else{
                no_loss = false;
                timeout_recovery = true;
            }
        }
    }
    
    
    bool transmission_check(){
        return (cwnd_increment == last_cwnd);
    }

    void set_recovery(bool recovery_signal){
        timeout_recovery = recovery_signal;
    };

    size_t cwnd_expect(){
        ssize_t expect_cwnd_ = INI_WIN;
        if(is_slow_start){
            if (congestion_window < ssthread){
		    expect_cwnd_ = congestion_window * 2;
            }else{
                expect_cwnd_ = congestion_window + PACKET_SIZE;
            }
        }else{
            expect_cwnd_ = C * std::pow(cubic_time - K, 3.0) + W_last_max;
        }
        if (expect_cwnd_ < INI_WIN){
            expect_cwnd_ = INI_WIN;
        }
        return expect_cwnd_;
    }

    size_t cwnd(){
        if (timeout_recovery){
            if (congestion_window / 2 > INI_WIN){
                ssthread = congestion_window / 2;
            }   
            congestion_window = INI_WIN;
            W_max = congestion_window;
            change_status(false);
        }else{
            if (no_loss == true){
                if (is_slow_start){
                    if (congestion_window < ssthread){
                        if (congestion_window == 0){
                            congestion_window = INI_WIN;
                        }else{
                            congestion_window *= 2;
                        }     
                    }else{
                        congestion_window += PACKET_SIZE;
                    }
                }

                if (is_congestion){
                    congestion_window = C * std::pow(cubic_time++ - K, 3.0) + W_last_max;
                }
                W_max = congestion_window;
                
            }else{
                K = std::cbrt(W_max * BETA / C);
                cubic_time = 1;
                congestion_window = C * std::pow(cubic_time++ - K, 3.0) + W_max;
                W_last_max = W_max;
                change_status(true);
            }
        }
        if (congestion_window < INI_WIN){
            congestion_window = INI_WIN;
        }

        if (congestion_window == INI_WIN){
            change_status(false);
        }
        set_recovery(false);
        parameter_reset();
        return congestion_window;
    }
    
    void parameter_reset(){
        incre_win = 0;
        decre_win = 0;
        incre_win_copy = 0;
        decre_win_copy = 0;
        bytes_in_flight = 0;
        cwnd_increment = 0;
    }

};

}
