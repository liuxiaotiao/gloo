// use std::cmp;

use std::str::FromStr;

use std::time::Duration;
use std::time::Instant;

use crate::Config;


// use crate::frame;
// use crate::packet;
// use crate::minmax;


#[cfg(feature = "qlog")]
use qlog::events::EventData;

// use self::NewCubic::State;

// Loss Recovery
// const C: f64 = 8.0;

// const INITIAL_TIME_THRESHOLD: f64 = 9.0 / 8.0;

// const GRANULARITY: Duration = Duration::from_millis(1);

const INITIAL_RTT: Duration = Duration::from_millis(333);

// const PERSISTENT_CONGESTION_THRESHOLD: u32 = 3;

// const RTT_WINDOW: Duration = Duration::from_secs(300);

// const MAX_PTO_PROBES_COUNT: usize = 2;

// Congestion Control
// const INITIAL_WINDOW_PACKETS: usize = 10;
const INITIAL_WINDOW_PACKETS: usize = 8;

const ELICT_ACK_CONSTANT: usize = 8;
const INI_WIN: usize = 1200 * 8;
const MINIMUM_WINDOW_PACKETS: usize = 2;

// const LOSS_REDUCTION_FACTOR: f64 = 0.5;

// const PACING_MULTIPLIER: f64 = 1.25;

// How many non ACK eliciting packets we send before including a PING to solicit
// an ACK.
// const MAX_OUTSTANDING_NON_ACK_ELICITING: usize = 24;

// const RECORD_LEN:usize = u16::MAX as usize;

const PACKET_SIZE: usize = 1200;
#[derive(Copy, Clone)]
pub struct Recovery {
    ///befre min_acked_pkt number, all packetes are processed, received or timeout
    // min_acked_pkt: u64,

    // max_acked_pkt: u64,
    //All packets has default priority is 3, and this priority recording will be retained untill one iteration training data is transmitted.
    // pkt_priority: [u8;RECORD_LEN],
    
    // priority_record: [u8;RECORD_LEN],
    /// ask last 8 sent packets
    // ack_pkts: [u64;8],

    loss_detection_timer: Option<Instant>,

    cubic_state: NewCubic::State,

    latest_rtt: Duration,

    smoothed_rtt: Option<Duration>,

    rttvar: Duration,

    // time_of_last_sent_ack_eliciting_pkt:
    //     [Option<Instant>; 3],

    // largest_acked_pkt: [u64; 3],

    // largest_sent_pkt: [u64; 3],
  
    // minmax_filter: minmax::Minmax<Duration>,

    min_rtt: Duration,

    loss_time: [Option<Instant>; 3],


    pub lost_count: usize,

    pub loss_probes: [usize; 3],

    in_flight_count: [usize; 3],

    app_limited: bool,

    cc_ops: &'static CongestionControlOps,

    congestion_window: usize,

    bytes_in_flight: usize,

    max_datagram_size: usize,
    k:f64,
    incre_win: usize,

    decre_win: usize,
    former_win: usize,
}

pub struct RecoveryConfig {
    max_send_udp_payload_size: usize,
    pub max_ack_delay: Duration,
    cc_ops: &'static CongestionControlOps,

}

impl RecoveryConfig {
    pub fn from_config(config: &Config) -> Self {
        Self {
            max_send_udp_payload_size: config.max_send_udp_payload_size,
            max_ack_delay: Duration::ZERO,
            cc_ops: config.cc_algorithm.into(),

        }
    }
}

impl Recovery {
    pub fn new_with_config(recovery_config: &RecoveryConfig) -> Self {
        let initial_congestion_window =
            recovery_config.max_send_udp_payload_size * INITIAL_WINDOW_PACKETS;

        Recovery {
            loss_detection_timer: None,

            // time_of_last_sent_ack_eliciting_pkt: [None; 3],

            // largest_acked_pkt: [std::u64::MAX; 3],

            // largest_sent_pkt: [0; 3],

            latest_rtt: Duration::ZERO,

            // This field should be initialized to `INITIAL_RTT` for the initial
            // PTO calculation, but it also needs to be an `Option` to track
            // whether any RTT sample was received, so the initial value is
            // handled by the `rtt()` method instead.
            smoothed_rtt: None,

            // minmax_filter: minmax::Minmax::new(Duration::ZERO),

            min_rtt: Duration::ZERO,

            rttvar: INITIAL_RTT / 2,

            loss_time: [None; 3],

            lost_count: 0,

            loss_probes: [0; 3],

            in_flight_count: [0; 3],

            congestion_window: initial_congestion_window,

            bytes_in_flight: 0,

            cc_ops: recovery_config.cc_ops,


            cubic_state: NewCubic::State::default(),

            app_limited: false,

            // min_acked_pkt:0,
            // max_acked_pkt:0,

            // pkt_priority: [0; RECORD_LEN],
            // priority_record: [0; RECORD_LEN],

            max_datagram_size:PACKET_SIZE,
            // ack_pkts: [0;8],

            k: 1.2,
            incre_win: 0,
            decre_win: 0,
            former_win: INI_WIN,
        }
    }

    pub fn new(config: &Config) -> Self {
        Self::new_with_config(&RecoveryConfig::from_config(config))
    }

    pub fn on_init(&mut self) {
        (self.cc_ops.on_init)(self);
    }

    pub fn reset(&mut self) {
        self.congestion_window = self.max_datagram_size * INITIAL_WINDOW_PACKETS;
        self.in_flight_count = [0; 3];
        // self.congestion_recovery_start_time = None;
        // self.ssthresh = std::usize::MAX;
        (self.cc_ops.reset)(self);

    }

    // cwnd = C(-(P - K))^3/k^3 + Wmax
    //x = [0:0.1:3.6];
    //y = -8*(x-1.8).^3/1.8^3;
    pub fn update_win(&mut self,weights:f32, num: f64){
       /* let test1 = (weights as f64- (self.k*num)/8.0).powi(3);
        let test2 = (weights as f64- (self.k*num)/8.0).powi(3)/self.k.powi(3);
        let test3 = -num * (weights as f64- (self.k*num)/8.0).powi(3)/self.k.powi(3);
        println!("weights: {:?}, num: {:?},smallwin: {:?}", weights, num,test3);*/
       // let winadd = (-C * (weights as f64- (self.k*num)/8.0).powi(3)/self.k.powi(3)) * self.max_datagram_size as f64;
       let winadd = (-num * (weights as f64 - (self.k*num)/8.0).powi(3)/((self.k*num)/8.0).powi(3)) * self.max_datagram_size as f64;
       //self.congestion_window += ((winadd/4.0)*4.0) as usize;
       if winadd > 0.0{
            self.incre_win += winadd as usize;
        }else {
            self.decre_win += (-winadd) as usize;
        }
    }


    /// Returns whether or not we should elicit an ACK even if we wouldn't
    /// otherwise have constructed an ACK eliciting packet.
    pub fn should_elicit_ack(&self, pkt_num:usize) -> bool {

        if pkt_num % ELICT_ACK_CONSTANT == 0{
            true
        }else{
            false
        }
    }


    pub fn loss_detection_timer(&self) -> Option<Instant> {
        self.loss_detection_timer
    }
    ///modified
    pub fn cwnd(&mut self) -> usize {
        println!("incre_win:{:?}, decre_win:{:?}",self.incre_win,self.decre_win);
        let mut tmp_win:usize=0;
        if 2*self.incre_win > self.decre_win{
            tmp_win = 2*self.incre_win - self.decre_win;
        }else{
            tmp_win = 0;
        }
        self.congestion_window = tmp_win;
        println!("add: {:?}, minus: {:?}, tmp_win: {:?}", self.incre_win, self.decre_win, tmp_win);
                if self.decre_win == 0{
                    println!("former_win: {:?}",tmp_win);
            self.former_win = tmp_win;
        }
        self.incre_win = 0;
        self.decre_win = 0;
        if self.congestion_window >=  PACKET_SIZE*INITIAL_WINDOW_PACKETS{
            self.congestion_window
        }else{
            self.congestion_window = PACKET_SIZE*INITIAL_WINDOW_PACKETS;
            self.congestion_window
        }
        
    }
    //modified
    pub fn cwnd_available(&self) -> usize {
        // Open more space (snd_cnt) for PRR when allowed.
        self.congestion_window.saturating_sub(self.bytes_in_flight)
    }

    // fn update_rtt(
    //     &mut self, latest_rtt: Duration,  now: Instant,
    // ) {
    //     self.latest_rtt = latest_rtt;

    //     match self.smoothed_rtt {
    //         // First RTT sample.
    //         None => {
    //             self.min_rtt = self.minmax_filter.reset(now, latest_rtt);

    //             self.smoothed_rtt = Some(latest_rtt);

    //             self.rttvar = latest_rtt / 2;
    //         },

    //         Some(srtt) => {
    //             self.min_rtt =
    //                 self.minmax_filter.running_min(RTT_WINDOW, now, latest_rtt);


    //             // Adjust for ack delay if plausible.
    //             let adjusted_rtt = latest_rtt;

    //             self.rttvar = self.rttvar.mul_f64(3.0 / 4.0) +
    //                 sub_abs(srtt, adjusted_rtt).mul_f64(1.0 / 4.0);

    //             self.smoothed_rtt = Some(
    //                 srtt.mul_f64(7.0 / 8.0) + adjusted_rtt.mul_f64(1.0 / 8.0),
    //             );
    //         },
    //     }
    // }

    pub fn collapse_cwnd(&mut self) {
        (self.cc_ops.collapse_cwnd)(self);
    }

    pub fn update_app_limited(&mut self, v: bool) {
        self.app_limited = v;
    }

    pub fn app_limited(&self) -> bool {
        self.app_limited
    }

        pub fn rollback(&mut self) -> usize{
        self.congestion_window = self.former_win;
        self.congestion_window
    }

    // pub fn delivery_rate_update_app_limited(&mut self, v: bool) {
    //     self.delivery_rate.update_app_limited(v);
    // }

}

/// Available congestion control algorithms.
///
/// This enum provides currently available list of congestion control
/// algorithms.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
#[repr(C)]
pub enum CongestionControlAlgorithm {
    /// CUBIC congestion control algorithm (default). `cubic` in a string form.
    NEWCUBIC = 1,

}

impl FromStr for CongestionControlAlgorithm {
    type Err = crate::Error;

    /// Converts a string to `CongestionControlAlgorithm`.
    ///
    /// If `name` is not valid, `Error::CongestionControl` is returned.
    fn from_str(name: &str) -> std::result::Result<Self, Self::Err> {
        match name {
            "newcubic" => Ok(CongestionControlAlgorithm::NEWCUBIC),
            _ => Err(crate::Error::CongestionControl),
        }
    }
}


pub struct CongestionControlOps {
    pub on_init: fn(r: &mut Recovery),

    pub reset: fn(r: &mut Recovery),

    pub collapse_cwnd: fn(r: &mut Recovery),

    pub checkpoint: fn(r: &mut Recovery),

    pub rollback: fn(r: &mut Recovery) -> bool,

    pub has_custom_pacing: fn() -> bool,

    pub debug_fmt:
        fn(r: &Recovery, formatter: &mut std::fmt::Formatter) -> std::fmt::Result,
}

impl From<CongestionControlAlgorithm> for &'static CongestionControlOps {
    fn from(algo: CongestionControlAlgorithm) -> Self {
        match algo {
            CongestionControlAlgorithm::NEWCUBIC => &NewCubic::NEWCUBIC,
        }
    }
}

impl std::fmt::Debug for Recovery {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self.loss_detection_timer {
            Some(v) => {
                let now = Instant::now();

                if v > now {
                    let d = v.duration_since(now);
                    write!(f, "timer={:?} ", d)?;
                } else {
                    write!(f, "timer=exp ")?;
                }
            },

            None => {
                write!(f, "timer=none ")?;
            },
        };

        write!(f, "latest_rtt={:?} ", self.latest_rtt)?;
        write!(f, "srtt={:?} ", self.smoothed_rtt)?;
        write!(f, "min_rtt={:?} ", self.min_rtt)?;
        write!(f, "rttvar={:?} ", self.rttvar)?;
        write!(f, "loss_time={:?} ", self.loss_time)?;
        write!(f, "loss_probes={:?} ", self.loss_probes)?;
        write!(f, "cwnd={} ", self.congestion_window)?;
        write!(f, "bytes_in_flight={} ", self.bytes_in_flight)?;
        write!(f, "app_limited={} ", self.app_limited)?;
        (self.cc_ops.debug_fmt)(self, f)?;

        Ok(())
    }
}


#[derive(Clone)]
pub struct Acked {
    pub pkt_num: [u64;8],

    pub time_recived: Instant,

    pub priority_loss: usize,
}



// fn sub_abs(lhs: Duration, rhs: Duration) -> Duration {
//     if lhs > rhs {
//         lhs - rhs
//     } else {
//         rhs - lhs
//     }
// }

mod NewCubic;
