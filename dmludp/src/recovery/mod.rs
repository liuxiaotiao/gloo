use std::str::FromStr;

use std::time::Duration;
use std::time::Instant;

use crate::Config;
use std::collections::BTreeSet;


#[cfg(feature = "qlog")]
use qlog::events::EventData;

const INITIAL_RTT: Duration = Duration::from_millis(333);

// Congestion Control
const INITIAL_WINDOW_PACKETS: usize = 8;

const ELICT_ACK_CONSTANT: usize = 8;

const MINIMUM_WINDOW_PACKETS: usize = 2;

const INI_WIN: usize = 1200 * 8;

const PACKET_SIZE: usize = 1200;
// #[derive(Copy, Clone)]
#[derive(Clone)]
pub struct Recovery {

    loss_detection_timer: Option<Instant>,

    cubic_state: NewCubic::State,

    latest_rtt: Duration,

    smoothed_rtt: Option<Duration>,

    rttvar: Duration,

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
    // k:f64,
    incre_win: usize,

    decre_win: usize,

    former_win_vecter: BTreeSet<usize>,

    roll_back_flag: bool,

    function_change: bool,

    incre_win_copy: usize,

    decre_win_copy: usize,

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

            latest_rtt: Duration::ZERO,

            // This field should be initialized to `INITIAL_RTT` for the initial
            // PTO calculation, but it also needs to be an `Option` to track
            // whether any RTT sample was received, so the initial value is
            // handled by the `rtt()` method instead.
            smoothed_rtt: None,

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

            max_datagram_size:PACKET_SIZE,

            incre_win: 0,
            decre_win: 0,

            former_win_vecter:BTreeSet::<usize>::new(),

            roll_back_flag: false,

            function_change: false,

            incre_win_copy: 0,

            decre_win_copy: 0,

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

        (self.cc_ops.reset)(self);

    }

    // cwnd = C(-(P - K))^3/k^3 + Wmax
    //x = [0:0.1:3.6];
    //y = -8*(x-1.8).^3/1.8^3;
    pub fn update_win(&mut self,weights:f32, num: f64){
        let mut winadd_copy = 0.0;
        let winadd =  if self.function_change{
            (3.0 * (weights as f64).powi(2) - 12.0 * (weights as f64) + 4.0) * self.max_datagram_size as f64 
        }else {
            if weights > 0.0{
                self.function_change = true;
                self.incre_win = self.incre_win_copy;
                self.decre_win = self.decre_win_copy;
            }
            winadd_copy = (3.0*(weights as f64).powi(2) -12.0*(weights as f64) + 4.0) * self.max_datagram_size as f64; 
            num * self.max_datagram_size as f64
        };
       
        if winadd != num{
            self.roll_back_flag = true;
        }

        if winadd > 0.0{
            self.incre_win += winadd as usize;
        }else {
            self.decre_win += (-winadd) as usize;
        }

        if winadd_copy > 0.0{
            self.incre_win_copy += winadd_copy as usize;
        }else {
            self.decre_win_copy += (-winadd_copy) as usize;
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
        let tmp_win:usize = if 2*self.incre_win > self.decre_win{
            2*self.incre_win - self.decre_win
        }else{
            0
        };
        if !self.roll_back_flag {
            self.former_win_vecter.insert(tmp_win);
        }

        self.congestion_window = tmp_win;
        self.incre_win = 0;
        self.decre_win = 0;
        self.incre_win_copy = 0;
        self.decre_win_copy = 0;
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
        if self.former_win_vecter.is_empty(){
            self.congestion_window = INI_WIN;
        }else{
            self.congestion_window = self.former_win_vecter.pop_last().unwrap();
        }
        
        self.congestion_window
    }

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


mod NewCubic;

