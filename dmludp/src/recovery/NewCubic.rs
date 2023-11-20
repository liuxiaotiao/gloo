// use std::time::Instant;

use crate::recovery;

use crate::recovery::CongestionControlOps;
use crate::recovery::Recovery;


// Δwindow = -8*(x-1.8).^3/(1.8)^3; 
// Δwindow = C(-(P - K))^3/k^3
// c = 2
// cwnd = C(-(P - K))^3/k^3 + Wmax
// K = 3*max(priority)
pub static NEWCUBIC: CongestionControlOps = CongestionControlOps {
    on_init,
    reset,
    // on_packet_sent,
    // on_packets_acked,
    // congestion_event,
    collapse_cwnd,
    checkpoint,
    rollback,
    has_custom_pacing,
    debug_fmt,
};

/// CUBIC Constants.
///
/// These are recommended value in RFC8312.
// const BETA_CUBIC: f64 = 0.7;

// const C: f64 = 8.0;

// const MAX_WEIGHT:f64 = 0.45;
// const MIDIUM_WEIGHT:f64 = 0.2;
// const MIN_WEIGHT:f64 = 0.1;

/// Default value of alpha_aimd in the beginning of congestion avoidance.
// const ALPHA_AIMD: f64 = 3.0 * (1.0 - BETA_CUBIC) / (1.0 + BETA_CUBIC);

// const OVER_WINGOW: f64 = 1.2;

/// CUBIC State Variables.
///
/// We need to keep those variables across the connection.
/// k, w_max, w_est are described in the RFC.
#[derive(Debug, Default,Clone, Copy)]
pub struct State {
    k: f64,

    w_max: f64,

    // Used in CUBIC fix (see on_packet_sent())
    // last_sent_time: Option<Instant>,

    // Store cwnd increment during congestion avoidance.
    // cwnd_inc: usize,

    // CUBIC state checkpoint preceding the last congestion event.
    prior: PriorState,
}

/// Stores the CUBIC state from before the last congestion event.
///
/// <https://tools.ietf.org/id/draft-ietf-tcpm-rfc8312bis-00.html#section-4.9>
#[derive(Debug, Default,Clone, Copy)]
struct PriorState {
    congestion_window: usize,

    ///ssthresh: usize,

    w_max: f64,

    k: f64,

    // epoch_start: Option<Instant>,

    lost_count: usize,
}

/// CUBIC Functions.
///
/// Note that these calculations are based on a count of cwnd as bytes,
/// not packets.
/// Unit of t (duration) and RTT are based on seconds (f64).
impl State {
  
    // K = cubic_root ((w_max - cwnd) / C) (Eq. 2)
    // K = 3*max(priority)
    //fn cubic_k(&self, cwnd: usize, priority: usize) -> f64 {
    // fn cubic_k(&self, cwnd: usize, priority_list: [u8;3]) -> f64 {

    //     // let w_max = self.w_max / max_datagram_size as f64;
    //     // let cwnd = cwnd as f64 / max_datagram_size as f64;

    //     // libm::cbrt((w_max - cwnd) / C)
    //     8.0*MAX_WEIGHT as f64
    // }

    // W_cubic(t) = C * (t - K)^3 + w_max (Eq. 1)

    // cwnd = C(-(P - K))^3/k^3 + Wmax
    //x = [0:0.1:3.6];
    //y = -8*(x-1.8).^3/1.8^3;
    // pub fn w_cubic(&self, p: f64, max_datagram_size: usize) -> f64 {
    //     let w_max = self.w_max / max_datagram_size as f64;

    //     // (C * (t.as_secs_f64() - self.k).powi(3) + w_max) *
    //     //     max_datagram_size as f64
    //     (-C * (p - self.k).powi(3)/self.k.powi(3) + w_max) *
    //         max_datagram_size as f64
    // }


}

fn on_init(_r: &mut Recovery) {}

fn reset(r: &mut Recovery) {
    r.cubic_state = State::default();
}


// pub fn on_packet_acked(
//     r: &mut Recovery, priority_loss: f64, 
// )->f64 {
//     // if priority_loss == 0{
//         r.cubic_state.prior.congestion_window = r.congestion_window;
//     // }
//     // else{
//     //     if r.congestion_window == OVER_WINGOW*r.cubic_state.PriorState.congestion_window{
//     //         r.congestion_window = r.cubic_state.PriorState.congestion_window;
//     //     }
        
//     // }
//     let target = r.cubic_state.w_cubic(priority_loss, r.max_datagram_size);
//     target  
// }

///modified
fn collapse_cwnd(r: &mut Recovery) {
    let newcubic = &mut r.cubic_state;

    newcubic.w_max = r.congestion_window as f64;


    // NEWCUBIC.cwnd_inc = 0;

    //reno::collapse_cwnd(r);
    r.congestion_window = r.max_datagram_size * recovery::MINIMUM_WINDOW_PACKETS;

}

//modified
// pub fn on_packet_sent(r: &mut Recovery, pkt_num:u64, pkt_priority:u8, sent_bytes: usize, now: Instant) {
//     // See https://github.com/torvalds/linux/commit/30927520dbae297182990bb21d08762bcc35ce1d
//     // First transmit when no packets in flight
//     // let Newcubic = &mut r.newcubic_state;

//     if r.pkt_priority[pkt_num as usize] != 0{
 
//         r.priority_record[pkt_num as usize] -= 1
  
//     }else{
//         r.pkt_priority[pkt_num as usize] = pkt_priority;
//         r.priority_record[pkt_num as usize] = pkt_priority;
//     }
//     r.bytes_in_flight += sent_bytes;
// }



fn checkpoint(r: &mut Recovery) {
    r.cubic_state.prior.congestion_window = r.congestion_window;
    r.cubic_state.prior.w_max = r.cubic_state.w_max;
    r.cubic_state.prior.k = r.cubic_state.k;
    ////
    // r.cubic_state.prior.epoch_start = r.congestion_recovery_start_time;
    r.cubic_state.prior.lost_count = r.lost_count;
}

///may be discarded later
fn rollback(r: &mut Recovery) -> bool {

    r.congestion_window = r.cubic_state.prior.congestion_window;
    r.cubic_state.w_max = r.cubic_state.prior.w_max;
    r.cubic_state.k = r.cubic_state.prior.k;
    // r.congestion_recovery_start_time = r.cubic_state.prior.epoch_start;

    true
}

fn has_custom_pacing() -> bool {
    false
}

fn debug_fmt(r: &Recovery, f: &mut std::fmt::Formatter) -> std::fmt::Result {
    write!(
        f,
        "cubic={{ k={} w_max={} }} ",
        r.cubic_state.k, r.cubic_state.w_max
    )
}