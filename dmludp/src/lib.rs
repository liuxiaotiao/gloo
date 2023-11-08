use std::cmp;
// use std::cmp::min;
// use std::collections::btree_map::Range;
use std::convert::TryInto;
// use std::result;
use std::time;
use std::time::Duration;
use std::time::Instant;
use std::net::SocketAddr;

use std::str::FromStr;

// use std::collections::HashSet;
use std::collections::VecDeque;
// use std::collections::hash_map;
use std::collections::BTreeMap;
// use std::collections::BinaryHeap;
use std::collections::HashMap;
// use std::vec;
// use rand::Rng;
// use std::ops::Bound::Included;

const HEADER_LENGTH: usize = 26;

const ELICT_FLAG: usize = 8;
// use crate::ranges;
const CONGESTION_THREAHOLD: f64 = 0.01;

/// The minimum length of Initial packets sent by a client.
pub const MIN_CLIENT_INITIAL_LEN: usize = 1350;

// #[cfg(not(feature = "fuzzing"))]
// const PAYLOAD_MIN_LEN: usize = 4;

// The default max_datagram_size used in congestion control.
const MAX_SEND_UDP_PAYLOAD_SIZE: usize = 1350;

const SEND_BUFFER_SIZE:usize = 1024;

pub type Result<T> = std::result::Result<T, Error>;

/// A QUIC error.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    /// There is no more work to do.
    Done,

    /// The provided buffer is too short.
    BufferTooShort,

    /// The provided packet cannot be parsed because its version is unknown.
    UnknownVersion,

    /// The provided packet cannot be parsed because it contains an invalid
    /// frame.
    InvalidFrame,

    /// The provided packet cannot be parsed.
    InvalidPacket,

    /// The operation cannot be completed because the connection is in an
    /// invalid state.
    InvalidState,

    /// The operation cannot be completed because the stream is in an
    /// invalid state.
    ///
    /// The stream ID is provided as associated data.
    InvalidStreamState(u64),

    /// The peer's transport params cannot be parsed.
    InvalidTransportParam,

    /// A cryptographic operation failed.
    CryptoFail,

    /// The TLS handshake failed.
    TlsFail,

    /// The peer violated the local flow control limits.
    FlowControl,

    /// The peer violated the local stream limits.
    StreamLimit,

    /// The specified stream was stopped by the peer.
    ///
    /// The error code sent as part of the `STOP_SENDING` frame is provided as
    /// associated data.
    StreamStopped(u64),

    /// The specified stream was reset by the peer.
    ///
    /// The error code sent as part of the `RESET_STREAM` frame is provided as
    /// associated data.
    StreamReset(u64),

    /// The received data exceeds the stream's final size.
    FinalSize,

    /// Error in congestion control.
    CongestionControl,

    /// Too many identifiers were provided.
    IdLimit,

    /// Not enough available identifiers.
    OutOfIdentifiers,

    Stopped,
}

impl Error {
    // fn to_wire(self) -> u64 {
    //     match self {
    //         Error::Done => 0x0,
    //         Error::InvalidFrame => 0x7,
    //         Error::InvalidStreamState(..) => 0x5,
    //         Error::InvalidTransportParam => 0x8,
    //         Error::FlowControl => 0x3,
    //         Error::StreamLimit => 0x4,
    //         Error::FinalSize => 0x6,
    //         Error::Stopped => 0x19,
    //         _ => 0xa,
    //     }
    // }

    #[cfg(feature = "ffi")]
    fn to_c(self) -> libc::ssize_t {
        match self {
            Error::Done => -1,
            Error::BufferTooShort => -2,
            Error::UnknownVersion => -3,
            Error::InvalidFrame => -4,
            Error::InvalidPacket => -5,
            Error::InvalidState => -6,
            Error::InvalidStreamState(_) => -7,
            Error::InvalidTransportParam => -8,
            Error::CryptoFail => -9,
            Error::TlsFail => -10,
            Error::FlowControl => -11,
            Error::StreamLimit => -12,
            Error::FinalSize => -13,
            Error::CongestionControl => -14,
            Error::StreamStopped { .. } => -15,
            Error::StreamReset { .. } => -16,
            Error::IdLimit => -17,
            Error::OutOfIdentifiers => -18,
            Error::Stopped => -19
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        None
    }
}

impl std::convert::From<octets::BufferTooShortError> for Error {
    fn from(_err: octets::BufferTooShortError) -> Self {
        Error::BufferTooShort
    }
}

/// Ancillary information about incoming packets.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RecvInfo {
    /// The remote address the packet was received from.
    pub from: SocketAddr,

    /// The local address the packet was received on.
    pub to: SocketAddr,
}

/// Ancillary information about outgoing packets.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SendInfo {
    /// The local address the packet should be sent from.
    pub from: SocketAddr,

    /// The remote address the packet should be sent to.
    pub to: SocketAddr,

}

/// Stores configuration shared between multiple connections.
pub struct Config {

    cc_algorithm: CongestionControlAlgorithm,

    max_send_udp_payload_size: usize,

    max_idle_timeout: u64,
}

impl Config {
    /// Creates a config object with the given version.
    ///
    /// ## Examples:
    ///
    /// ```
    /// let config = quiche::Config::new()?;
    /// # Ok::<(), quiche::Error>(())
    /// ```
    pub fn new() -> Result<Config> {
        Ok(Config {
            // local_transport_params: TransportParams::default(),
            cc_algorithm: CongestionControlAlgorithm::NEWCUBIC,
            // pacing: true,

            max_send_udp_payload_size: MAX_SEND_UDP_PAYLOAD_SIZE,

            max_idle_timeout: 5000,
        })
    }

    /// Sets the `max_idle_timeout` transport parameter, in milliseconds.
    /// same with tcp max idle timeout
    /// The default value is infinite, that is, no timeout is used.
    pub fn set_max_idle_timeout(&mut self, v: u64) {
        self.max_idle_timeout = v;
    }
   
    /// Sets the congestion control algorithm used by string.
    ///
    /// The default value is `cubic`. On error `Error::CongestionControl`
    /// will be returned.
    ///
    /// ## Examples:
    ///
    /// ```
    // # let mut config = quiche::Config::new(0xbabababa)?;
    /// config.set_cc_algorithm_name("NEWCUBIC");
    /// # Ok::<(), quiche::Error>(())
    /// ```
    pub fn set_cc_algorithm_name(&mut self, name: &str) -> Result<()> {
        self.cc_algorithm = CongestionControlAlgorithm::from_str(name)?;

        Ok(())
    }

    /// Sets the congestion control algorithm used
    ///
    /// The default value is `CongestionControlAlgorithm::CUBIC`.
    pub fn set_cc_algorithm(&mut self, algo: CongestionControlAlgorithm) {
        self.cc_algorithm = algo;
    }

}

#[inline]
pub fn accept(
    local: SocketAddr,
    peer: SocketAddr, config: &mut Config,
) -> Result<Connection> {
    let conn = Connection::new(local, peer, config, true)?;

    Ok(conn)
}


#[inline]
pub fn connect(
    local: SocketAddr,
    peer: SocketAddr, config: &mut Config,
) -> Result<Connection> {
    let conn = Connection::new(local, peer, config, false)?;

    Ok(conn)
}

pub struct Connection {

    /// Total number of received packets.
    recv_count: usize,

    /// Total number of sent packets.
    sent_count: usize,

    /// Draining timeout expiration time.
    draining_timer: Option<time::Instant>,

    /// Whether this is a server-side connection.
    is_server: bool,

    /// Whether the connection handshake has been completed.
    handshake_completed: bool,

    /// Whether the connection handshake has been confirmed.
    handshake_confirmed: bool,

    /// Whether the connection is closed.
    closed: bool,

    // Whether the connection was timed out
    timed_out: bool,

    #[cfg(feature = "qlog")]
    qlog: QlogInfo,

    server: bool,

    localaddr: SocketAddr,

    peeraddr: SocketAddr,

    recovery: Recovery,

    pkt_num_spaces: [packet::PktNumSpace; 2],

    pub rtt: time::Duration,
    
    handshake: time::Instant,

    send_buffer: SendBuf,

    rec_buffer: RecvBuf,

    written_data: usize,

    stop_flag: bool,

    stop_ack: bool,

    prioritydic: HashMap<u64,u8>,

    sent_pkt: Vec<u64>,

    recv_dic: HashMap<u64,u8>,

    //used to compute priority
    low_split_point: f32,

    high_split_point: f32,

    //store data
    send_data:Vec<u8>,

    //store norm2 for every 256 bits float
    norm2_vec:Vec<f32>,

    total_offset:u64,

    recv_flag: bool,

    recv_hashmap: HashMap<u64, u64>,

    feed_back: bool,

    ack_point: usize,

    max_off: u64,

    send_num: u64,

    high_priority: usize,

    sent_number: usize,

    recv_pkt_sent_num: Vec<usize>,

    sent_dic: HashMap<u64, u64>,
}

impl Connection {
    /// Sets the congestion control algorithm used by string.
    /// Examples:
    /// let mut config = quiche::Config::new()?;
    /// let local = "127.0.0.1:0".parse().unwrap();
    /// let peer = "127.0.0.1:1234".parse().unwrap();
    /// let conn = quiche::new(local, peer, &mut config, true)?
    /// # Ok::<(), quiche::Error>(())
    /// ```
    fn new(
        local: SocketAddr,
        peer: SocketAddr, config: &mut Config, is_server: bool,
    ) ->  Result<Connection> {


        let mut conn = Connection {

            pkt_num_spaces: [
                packet::PktNumSpace::new(),
                packet::PktNumSpace::new(),
            ],

            recv_count: 0,

            sent_count: 0,

            draining_timer: None,

            is_server,

            // Assume clients validate the server's address implicitly.
            handshake_completed: false,

            handshake_confirmed: true,

            closed: false,

            timed_out: false,

            server: is_server,

            localaddr: local,

            peeraddr: peer,

            recovery: recovery::Recovery::new(&config),

            rtt: Duration::ZERO,
            
            handshake: Instant::now(),
            
            send_buffer: SendBuf::new((MIN_CLIENT_INITIAL_LEN*8).try_into().unwrap()),

            rec_buffer: RecvBuf::new(),
            
            written_data:0,

            //All buffer data have been sent, waiting for the ack responce.
            stop_flag: false,
            stop_ack: false,

            prioritydic:HashMap::new(),

            recv_dic:HashMap::new(),

            sent_pkt:Vec::<u64>::new(),
            
            low_split_point:0.0,
            high_split_point:0.0,

            send_data:Vec::<u8>::new(),
            norm2_vec:Vec::<f32>::new(),

            total_offset:0,

            recv_flag: false,

            recv_hashmap:HashMap::new(),

            feed_back: false,

            ack_point: 0,

            max_off: 0,

            send_num: 0,

            high_priority: 0,

            sent_number: 0,

            recv_pkt_sent_num:Vec::<usize>::new(),

            sent_dic:HashMap::new(),
        };

        conn.recovery.on_init();
 

        Ok(conn)
    }

    fn update_rtt(&mut self){
        let arrive_time = Instant::now();
        if self.rtt == Duration::ZERO{
            self.rtt = arrive_time.duration_since(self.handshake);
        }else{
            // self.rtt = self.rtt/8 + 15*arrive_time.duration_since(self.handshake)/16;
            self.rtt = 17*arrive_time.duration_since(self.handshake)/16;
        }
        
    }

    pub fn new_rtt(& mut self, last: Duration){
        // println!("Pre rtt: {:?}, last: {:?}",self.rtt, last);
        self.rtt = self.rtt/4 + 3*last/4;
    }

    pub fn recv_slice(&mut self, buf: &mut [u8]) ->Result<usize>{
        let len = buf.len();

        if len == 0{
            return Err(Error::BufferTooShort);
        }
        self.recv_count += 1;

        let mut b = octets::OctetsMut::with_slice(buf);

        let hdr = Header::from_bytes(&mut b)?;

        let mut read:usize = 0;

        if hdr.ty == packet::Type::Handshake && self.is_server{
            self.update_rtt();
            self.handshake_completed = true;
        }
        
        //If receiver receives a Handshake packet, it will be papred to send a Handshank.
        if hdr.ty == packet::Type::Handshake && !self.is_server{
            self.handshake_confirmed = false;
            self.feed_back = true;
        }
        
        //receiver send back the sent info to sender
        if hdr.ty == packet::Type::ACK && self.is_server{
            //println!("{:?}",self.send_buffer.offset_index);
            self.process_ack(buf);
            //self.update_rtt();
        }

        if hdr.ty == packet::Type::ElictAck{
            self.recv_flag = true;
            self.send_num = u64::from_be_bytes(buf[1..9].try_into().unwrap());
            self.check_loss(&mut buf[26..]);
            self.feed_back = true;
        }

        if hdr.ty == packet::Type::Application{
            self.recv_count += 1;
            read = hdr.pkt_length as usize;
            self.rec_buffer.write(&mut buf[26..],hdr.offset).unwrap();
            // self.prioritydic.insert(hdr.offset, hdr.priority);
            self.recv_dic.insert(hdr.offset, hdr.priority);
            println!("offset: {:?}, length: {:?}", hdr.offset, hdr.pkt_length);
        }

        if hdr.ty == packet::Type::Stop{
            return Err(Error::Stopped);
        }

        if hdr.ty == packet::Type::Fin{
            return Err(Error::Done);
        }

        Ok(read)
    }

    pub fn send_ack(&self)->bool{
        self.feed_back
    }
    
    //Get unack offset. 
    fn process_ack(&mut self, buf: &mut [u8]){
        let unackbuf = &buf[26..];
        let max_ack = u64::from_be_bytes(unackbuf[..8].try_into().unwrap());
        if max_ack > self.max_off{
            self.max_off = max_ack;
        }
        let len = unackbuf.len();
        let mut start = 8;
        let mut weights:f32 = 0.0;
        // let mut b = octets::OctetsMut::with_slice(buf);
        let mut _ack_set:u64 = 0 ;
        while start < len{
            let unack = u64::from_be_bytes(unackbuf[start..start+8].try_into().unwrap());
            start += 8;
            let mut priority = u64::from_be_bytes(unackbuf[start..start+8].try_into().unwrap());
            if let Some(recviecd) = self.sent_dic.get(&unack){
                if *recviecd == 0{
                    self.send_buffer.ack_and_drop(unack);
                }
            }
            if priority != 0{
                priority = self.priority_calculation(unack) as u64;
            }
            start += 8;
            if priority == 1{
                weights += 0.15;
            }else if priority == 2 {
                weights += 0.2;
            }else if priority == 3 {
                weights += 0.25;
            }else{
                // println!("offset: {:?}, received",unack);
                self.send_buffer.ack_and_drop(unack);
            }
            let real_priority = self.priority_calculation(unack);
            // println!("offset: {:?}, priority: {:?}",unack,priority);
            // println!("offset: {:?}, real priority: {:?}",unack,self.priority_calculation(unack));
            if priority != 0 && real_priority == 3{
                self.high_priority += 1;
            }
        }

        // update congestion window size
        self.recovery.update_win(weights, (len/(8*2)) as f64);
        
    }

    pub fn findweight(&mut self, unack:&u64)->u8{
        *self.prioritydic.get(unack).unwrap()
    }

    pub fn send_all(&mut self) -> bool {
        self.stop_flag = false;
        self.stop_ack = false;
        
        /// need to change!!!!!!!!!
        self.set_handshake();
        ///
        if self.send_data.len() > self.written_data{
            let write = self.write();
            self.written_data += write.unwrap();
            self.total_offset += write.unwrap() as u64;

            return true
        }else {
            if self.send_buffer.data.is_empty(){
                return false
            }else{
            let write = self.write();
            self.written_data += write.unwrap();
            self.total_offset += write.unwrap() as u64;
            return true}
        }
        
    }

    //Send single packet
    pub fn send_data(&mut self, out: &mut [u8])-> Result<(usize, SendInfo)>{
        if out.is_empty(){
            return Err(Error::BufferTooShort);
        }
        
        let done = 0;
        let mut total_len:usize = HEADER_LENGTH;

        // Limit output packet size to respect the sender and receiver's
        // maximum UDP payload size limit.
        let mut _left = cmp::min(out.len(), self.max_send_udp_payload_size());

        let mut pn:u64 = 0;
        let mut offset:u64 = 0;
        let mut priority:u8 = 0;
        let mut psize:u64 = 0;

        let ty = self.write_pkt_type()?; 

        let info = SendInfo {
            from: self.localaddr,
            to: self.peeraddr,
        };
        if ty == packet::Type::Handshake && self.server{
            let hdr = Header {
                ty,
                pkt_num: pn,
                offset: offset,
                priority: priority,
                pkt_length: psize,
            };
            let mut b = octets::OctetsMut::with_slice(out);
            hdr.to_bytes(&mut b)?;
            self.set_handshake();
        }

        if ty == packet::Type::Handshake && !self.server{
            let hdr = Header {
                ty,
                pkt_num: pn,
                offset: offset,
                priority: priority,
                pkt_length: psize,
            };
            let mut b = octets::OctetsMut::with_slice(out);
            hdr.to_bytes(&mut b)?;
            self.feed_back = false;
        }
    
        //send the received packet condtion
        if ty == packet::Type::ACK{
            self.feed_back = false;
            let mut b = octets::OctetsMut::with_slice(out);
            psize = (self.recv_hashmap.len()*8*2 + 8) as u64;
            let hdr = Header {
                ty,
                pkt_num: self.send_num,
                offset: 0,
                priority: 0,
                pkt_length: psize,
            };
            // offset = 8*16;
            hdr.to_bytes(&mut b)?;
            println!("ACk num: {:?}", self.send_num);
            let max_off = self.max_ack();
            b.put_u64(max_off)?;

            // pkt_length may not be 8
            for (key, val) in self.recv_hashmap.iter() {
                // let mut retrans = self.
                b.put_u64(*key)?;
                b.put_u64(*val)?;
            }

            self.recv_hashmap.clear();

        }

        //send the 
        if ty == packet::Type::ElictAck{
            pn =  self.pkt_num_spaces[1].next_pkt_num;
            self.pkt_num_spaces[1].next_pkt_num += 1;
            println!("ElickAck num: {:?}", pn);
            let mut b = octets::OctetsMut::with_slice(out);
            if self.stop_flag == true{
                let res = &self.sent_pkt[self.ack_point..];
                let pkt_counter = self.sent_pkt.len() - self.ack_point;
                let hdr = Header{
                    ty,
                    pkt_num: pn,
                    offset: offset,
                    priority: priority,
                    pkt_length: (pkt_counter*8) as u64,
                };
                hdr.to_bytes(&mut b).unwrap();
                for i in 0..res.len() as usize{
                    b.put_u64(res[i])?;
                    println!("ElictAck: {:?}",res[i]);
                }
                psize = (pkt_counter*8) as u64;
                self.ack_point = self.sent_pkt.len();
                self.stop_ack = true;
            }
            else{
                let res = &self.sent_pkt[self.ack_point..];
                let hdr = Header{
                    ty,
                    pkt_num: pn,
                    offset: offset,
                    priority: priority,
                    pkt_length: 64,
                };
                hdr.to_bytes(&mut b).unwrap();
                for i in 0..res.len() as usize{
                    b.put_u64(res[i])?;
                    println!("ElictAck: {:?}",res[i]);
                }
                self.ack_point = self.sent_pkt.len();
                psize = 64;
            }
            total_len += psize as usize;
            return Ok((total_len, info))
        }

        // Test later
        // if ty == packet::Type::ElictAck{
        //     let mut b = octets::OctetsMut::with_slice(out);
        //     //When send_buf send out all data
        //     let res = &self.sent_pkt[self.ack_point..];
        //     let pkt_counter = self.sent_pkt.len() - self.ack_point;
        //     let hdr = Header{
        //         ty,
        //         pkt_num: pn,
        //         offset: offset,
        //         priority: priority,
        //         pkt_length: (pkt_counter*8) as u64,
        //     };
        //     hdr.to_bytes(&mut b).unwrap();
        //     for i in 0..res.len() as usize{
        //         b.put_u64(res[i])?;
        //     }
        //     self.ack_point = self.sent_pkt.len();

        //     if self.stop_flag{
        //         self.stop_ack = true;
        //     }
                
        // }
        
        if ty == packet::Type::Application{
            if let Ok((result_len, off, stop)) = self.send_buffer.emit(&mut out[26..]){
                if off >= self.written_data.try_into().unwrap(){
                    return Err(Error::Done);
                }            
                self.sent_count += 1;
                self.sent_number += 1;
                let mut b = octets::OctetsMut::with_slice(&mut out[done..]);
                pn = self.pkt_num_spaces[0].next_pkt_num;
                println!("Application off: {:?}",off); 
                priority = self.priority_calculation(off);
                self.pkt_num_spaces[0].next_pkt_num += 1;
                if let Some(x) = self.sent_dic.get_mut(&off) {
                    *x -= 1;
                }else {
                    self.sent_dic.insert(off, priority as u64);
                }
                let hdr = Header {
                    ty,
                    pkt_num: pn,
                    offset: off,
                    priority: priority,
                    pkt_length: result_len as u64,
                };
                offset = result_len as u64;
                psize = result_len as u64;
                hdr.to_bytes(&mut b)?;

                //Recording offset of each data.
                // self.total_offset = std::cmp::max(off, self.total_offset);
    
                if stop == true{
                    self.stop_flag = true;
                }
                self.sent_pkt.push(hdr.offset);
            
            }
        }  

        if ty == packet::Type::Stop{
            let mut b = octets::OctetsMut::with_slice(out);
            let hdr = Header{
                ty,
                pkt_num: pn,
                offset: offset,
                priority: priority,
                pkt_length: psize,
            };

            hdr.to_bytes(&mut b)?;
            
            // total_len += offset as usize;
            total_len += psize as usize;
            return Ok((total_len, info));
        }

        if self.is_stopped() {
            return Err(Error::Done);
        }

        
        self.handshake = time::Instant::now();

        // total_len += offset as usize;
        total_len += psize as usize;
        Ok((total_len, info))
    }

    pub fn send_data_fin(&mut self, out: &mut [u8])-> Result<(usize, SendInfo)>{
        if out.is_empty(){
            return Err(Error::BufferTooShort);
        }
        
        let done = 0;
        let mut total_len:usize = HEADER_LENGTH;

        // Limit output packet size to respect the sender and receiver's
        // maximum UDP payload size limit.
        let mut _left = cmp::min(out.len(), self.max_send_udp_payload_size());

        let mut pn:u64 = 0;
        let mut offset:u64 = 0;
        let mut priority:u8 = 0;
        let mut psize:u64 = 0;

        let ty = packet::Type::Fin; 

        let info = SendInfo {
            from: self.localaddr,
            to: self.peeraddr,
        };
        if ty == packet::Type::Handshake && self.server{
            let hdr = Header {
                ty,
                pkt_num: pn,
                offset: offset,
                priority: priority,
                pkt_length: psize,
            };
            let mut b = octets::OctetsMut::with_slice(out);
            hdr.to_bytes(&mut b)?;
        }

        // total_len += offset as usize;
        Ok((total_len, info))
    }



    pub fn is_stopped(&self)->bool{
        self.stop_flag && self.stop_ack
    }

    //Start updating congestion control window and sending new data.
    pub fn is_ack(&self)->bool{
        let now = Instant::now();
        let interval = now.duration_since(self.handshake);
        if interval > self.rtt{
            return true;
        }
        false
    }

    pub fn read(&mut self, out:&mut [u8]) -> Result<usize>{
        self.rec_buffer.emit(out)

    }

    pub fn max_ack(&mut self) -> u64{
        self.rec_buffer.max_ack()
    }
  
    //Writing data to send buffer.
    pub fn write(&mut self) -> Result<usize> {
        //?/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        let toffset = self.total_offset % 1024;
        let mut off_len: usize = 0;
        if toffset % 1024 != 0{
            off_len = 1024 - (self.total_offset % 1024) as usize;
        }
        let high_ratio = self.high_priority as f64 / self.sent_number as f64;
        // println!("hight_ratio: {:?}", high_ratio);
        self.high_priority = 0;
        self.sent_number = 0;
        // let off_len = 1024 - (self.total_offset % 1024) as usize;
        //Note: written_data refers to the non-retransmitted data.
        let mut congestion_window = 0;
        if high_ratio > CONGESTION_THREAHOLD{
            congestion_window = self.recovery.rollback();
        }else{
            congestion_window = self.recovery.cwnd();
        }
        println!("cwnd: {:?}", congestion_window);
        self.send_buffer.write(&self.send_data[self.written_data..], congestion_window, off_len, self.max_off)
    }

    pub fn  priority_calculation(&self, off: u64) -> u8{
        let real_index = off/1024;
        if self.norm2_vec[real_index as usize] < self.low_split_point{
            1
        }
        else if self.norm2_vec[real_index as usize] < self.high_split_point {
            2
        }
        else {
            3
        }

    }

    pub fn reset(& mut self){
        self.norm2_vec.clear();
        self.send_buffer.clear();
    }

    ///responce packet used to tell sender which packet loss
    // fn send_ack(&mut self, recv_buf: &mut [u8],out:&mut [u8]) -> Header{
    //     let mut res:Vec<u64> = Vec::new(); 
    //     res = self.check_loss(recv_buf);  
    //     let pn:u64 = 0;
    //     let offset:u64 = 0;
    //     let priority:u8 = 0;
    //     let plen:u64 = res.len().try_into().unwrap();
    //     let psize:u64 = plen*4;
    //     let ty = packet::Type::ACK;
    //     let mut b = octets::OctetsMut::with_slice(out);
    //     //let pkt_type = packet::Type::ElictAck;
    //     let hdr= Header{
    //         ty: ty,
    //         pkt_num: pn,
    //         offset: offset,
    //         priority: priority,
    //         pkt_length: psize,
    //     };
    //     hdr.to_bytes(&mut b).unwrap();
    //     for i in 0..plen as usize{
    //         b.put_u64(res[i]).unwrap();
    //     } 
    //     hdr
    // }

    // fn record_reset(&mut self){
    
    // }

    pub fn check_loss(&mut self, recv_buf: &mut [u8]){
        let mut b = octets::OctetsMut::with_slice(recv_buf);
        // let result:Vec<u64> = Vec::new();
        while b.cap()>0 {
            let offset = b.get_u64().unwrap();
            if self.recv_dic.contains_key(&offset){
                self.recv_hashmap.insert(offset, 0);
            }else{
                self.recv_hashmap.insert(offset, 1);
            }
        }
    }

    pub fn set_handshake(&mut self){
        self.handshake = Instant::now();
    }

    pub fn get_rtt(&mut self) -> f64{
        self.rtt.as_secs_f64()*1_000_000_000.0
    }

    /// Returns the maximum possible size of egress UDP payloads.
    ///
    /// This is the maximum size of UDP payloads that can be sent, and depends
    /// on both the configured maximum send payload size of the local endpoint
    /// (as configured with [`set_max_send_udp_payload_size()`]), as well as
    /// the transport parameter advertised by the remote peer.
    ///
    /// Note that this value can change during the lifetime of the connection,
    /// but should remain stable across consecutive calls to [`send()`].
    ///
    /// [`set_max_send_udp_payload_size()`]:
    ///     struct.Config.html#method.set_max_send_udp_payload_size
    /// [`send()`]: struct.Connection.html#method.send
    pub fn max_send_udp_payload_size(&self) -> usize {
        MIN_CLIENT_INITIAL_LEN
    }
    

    /// Returns true if the connection handshake is complete.
    #[inline]
    pub fn is_established(&self) -> bool {
        self.handshake_completed
    }


    /// Returns true if the connection is draining.
    ///
    /// If this returns `true`, the connection object cannot yet be dropped, but
    /// no new application data can be sent or received. An application should
    /// continue calling the [`recv()`], [`timeout()`], and [`on_timeout()`]
    /// methods as normal, until the [`is_closed()`] method returns `true`.
    ///
    /// In contrast, once `is_draining()` returns `true`, calling [`send()`]
    /// is not required because no new outgoing packets will be generated.
    ///
    /// [`recv()`]: struct.Connection.html#method.recv
    /// [`send()`]: struct.Connection.html#method.send
    /// [`timeout()`]: struct.Connection.html#method.timeout
    /// [`on_timeout()`]: struct.Connection.html#method.on_timeout
    /// [`is_closed()`]: struct.Connection.html#method.is_closed
    #[inline]
    pub fn is_draining(&self) -> bool {
        self.draining_timer.is_some()
    }

    /// Returns true if the connection is closed.
    ///
    /// If this returns true, the connection object can be dropped.
    #[inline]
    pub fn is_closed(&self) -> bool {
        self.closed
    }

    /// Returns true if the connection was closed due to the idle timeout.
    #[inline]
    pub fn is_timed_out(&self) -> bool {
        self.timed_out
    }

    
    /// Selects the packet type for the next outgoing packet.
    fn write_pkt_type(& mut self) -> Result<packet::Type> {
        // let now = Instant::now();
        if self.rtt == Duration::ZERO && self.is_server == true{
            self.handshake_completed = true;
            return Ok(packet::Type::Handshake);
        }

        if self.handshake_confirmed == false && self.is_server == false{
            self.handshake_confirmed = true;
            return Ok(packet::Type::Handshake);
        }

        if (self.sent_count % ELICT_FLAG == 0 && self.sent_count > 0) || self.stop_flag == true{
            self.sent_count = 0;
            return Ok(packet::Type::ElictAck);
        }

        // if self.recv_count % 8 == 0 && self.recv_count > 0 {
        //     return Ok(packet::Type::ACK);
        // }
        if self.recv_flag == true{
            self.recv_flag = false;
            return Ok(packet::Type::ACK);
        }

        if self.rtt != Duration::ZERO{
            return Ok(packet::Type::Application);
        }

        if self.send_buffer.data.is_empty(){
            return Ok(packet::Type::Stop);
        }

        Err(Error::Done)
    }

    pub fn data_is_empty(& mut self)->bool{
        self.send_buffer.data.is_empty()
    }

    //read data from application
    pub fn data_send(& mut self, str_buf: & mut String){
        let mut output = str_buf.replace("\n", "");
        output = output.replace("\"", "");
        output = output.replace("\r","");
        let new_parts = output.split(">");
        self.norm2_vec.clear();
        self.send_data.clear();
        // let mut send_data:Vec<f32> = Vec::new();
        self.low_split_point = 0.0;
        self.high_split_point = 0.0;

        let mut norm2_tmp:Vec<f32>=Vec::new();
        for part in new_parts{
            if part == "" {
                break;
            }
            self.process_string(part.to_string(),&mut norm2_tmp);
        }
        norm2_tmp.sort_by(|a, b| a.partial_cmp(b).unwrap());
        self.low_split_point = norm2_tmp[norm2_tmp.len()*3/10 as usize];
        self.high_split_point = norm2_tmp[norm2_tmp.len()*7/10 as usize];
    }

    //store data and compute priority in advance
    pub fn process_string(& mut self, test_data: String, norm2_tmp: &mut Vec<f32>){
        let new_parts = test_data.split("[");
        let collection: Vec<&str> = new_parts.collect();
        let data = collection[1].to_string();
        let new_data = data.split("]");
        let collection: Vec<&str> = new_data.collect();
        let array_string = collection[0].to_string();
        let mut single_data: Vec<&str> = array_string.split(" ").collect();
        let white_space = "";
        single_data.retain(|&x| x != white_space);
    
        let mut norm2:f32 = 0.0;
        let mut it = 0;
        for item in single_data{
            it +=1;
            let my_float:f32 = FromStr::from_str(&item.to_string()).unwrap();
            norm2 +=my_float*my_float;
            let float_array = my_float.to_ne_bytes();
            self.send_data.extend(float_array);
            if it%256 == 255 {
                norm2_tmp.push(norm2);
                self.norm2_vec.push(norm2);
                norm2 = 0.0;
            }
        }
    
    }

}



#[derive(Clone, Debug, Eq, Default)]
pub struct RangeBuf {
    /// The internal buffer holding the data.
    ///
    /// To avoid needless allocations when a RangeBuf is split, this field is
    /// reference-counted and can be shared between multiple RangeBuf objects,
    /// and sliced using the `start` and `len` values.
    data: Vec<u8>,

    /// The initial offset within the internal buffer.
    start: usize,

    /// The current offset within the internal buffer.
    pos: usize,

    /// The number of bytes in the buffer, from the initial offset.
    len: usize,

    /// The offset of the buffer within a stream.
    off: u64,

}

impl RangeBuf {
    /// Creates a new `RangeBuf` from the given slice.
    pub fn from(buf: &[u8], off: u64) -> RangeBuf {
        RangeBuf {
            data: Vec::from(buf),
            start: 0,
            pos: 0,
            len: buf.len(),
            off,
        }
    }

    /// start refers to the start of all data.
    /// Returns the starting offset of `self`.
    /// Lowest offset of data, start == 0, pos == 0
    pub fn off(&self) -> u64 {
        (self.off - self.start as u64) + self.pos as u64
    }

    /// Returns the final offset of `self`.
    /// Returns the largest offset of the data
    pub fn max_off(&self) -> u64 {
        self.off() + self.len() as u64
    }

    /// Returns the length of `self`.
    pub fn len(&self) -> usize {
        // self.len - (self.pos - self.start)
        self.len - self.pos
    }

    /// Returns true if `self` has a length of zero bytes.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Consumes the starting `count` bytes of `self`.
    /// Consume() returns the max offset of the current data.
    pub fn consume(&mut self, count: usize) {
        self.pos += count;
    }

    /// Splits the buffer into two at the given index.
    pub fn split_off(&mut self, at: usize) -> RangeBuf {
        assert!(
            at <= self.len,
            "`at` split index (is {}) should be <= len (is {})",
            at,
            self.len
        );

        let buf = RangeBuf {
            data: self.data.clone()[at..].to_vec(),
            // start: self.start + at,
            // pos: cmp::max(self.pos, self.start + at),
            start: 0,
            pos: 0,
            len: self.len - at,
            off: self.off + at as u64,
        };

        self.pos = cmp::min(self.pos, self.start + at);
        self.len = at;

        buf
    }
}

impl std::ops::Deref for RangeBuf {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        &self.data[self.pos..self.start + self.len]
    }
}


impl PartialOrd for RangeBuf {
    fn partial_cmp(&self, other: &RangeBuf) -> Option<cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for RangeBuf {
    fn eq(&self, other: &RangeBuf) -> bool {
        self.off == other.off
    }
}

/// Receive-side stream buffer.
///
/// Stream data received by the peer is buffered in a list of data chunks
/// ordered by offset in ascending order. Contiguous data can then be read
/// into a slice.
#[derive(Debug, Default)]
pub struct RecvBuf {
    /// Chunks of data received from the peer that have not yet been read by
    /// the application, ordered by offset.
    data: BTreeMap<u64, RangeBuf>,

    /// The lowest data offset that has yet to be read by the application.
    off: u64,

    /// The total length of data received on this stream.
    len: u64,

    last_maxoff: u64,

    max_recv_off: u64,
}

impl RecvBuf {
    /// Creates a new receive buffer.
    fn new() -> RecvBuf {
        RecvBuf {
            ..RecvBuf::default()
        }
    }

    /// Inserts the given chunk of data in the buffer.
    ///
    /// This also takes care of enforcing stream flow control limits, as well
    /// as handling incoming data that overlaps data that is already in the
    /// buffer.
    pub fn write(&mut self, out: &mut [u8], out_off: u64) -> Result<()> {
        let buf = RangeBuf::from(out,out_off);

        let buf_len = buf.len();
        let tmp_off = buf.max_off()-buf_len as u64;
        if !self.data.is_empty(){
            let (k,_v) = self.data.last_key_value().unwrap();
            if *k == tmp_off{
                self.max_recv_off = tmp_off;
            }
        }   
        self.data.insert(buf.max_off(), buf);
        Ok(())
    }

    pub fn max_ack(&mut self)->u64{
        self.max_recv_off
    }

    /// Writes data from the receive buffer into the given output buffer.
    ///
    /// Only contiguous data is written to the output buffer, starting from
    /// offset 0. The offset is incremented as data is read out of the receive
    /// buffer into the application buffer. If there is no data at the expected
    /// read offset, the `Done` error is returned.
    ///
    /// On success the amount of data read, and a flag indicating if there is
    /// no more data in the buffer, are returned as a tuple.
    /// out should be initialed as 0
    pub fn emit(&mut self, out: &mut [u8]) -> Result<usize> {
        let mut len:usize = 0;
        let mut cap = out.len();

        if !self.ready() {
            return Err(Error::Done);
        }

        // The stream was reset, so return the error code instead.
        // if let Some(e) = self.error {
        //     return Err(Error::StreamReset(e));
        // }

        while cap > 0 && self.ready() {
            let mut entry = match self.data.first_entry() {
                Some(entry) => entry,
                None => break,
            };

            let buf = entry.get_mut();

            // let mut zero_vec = Vec::<u8>::new();

            let max_off = buf.max_off();
            let data_len = buf.len();
            let mut zero_len = 0;
            if max_off - self.last_maxoff != data_len.try_into().unwrap(){
                zero_len = max_off - self.last_maxoff - (data_len as u64);
                // let value = 0;
                // zero_vec.extend(std::iter::repeat(value).take(zero_len.try_into().unwrap()));
            }
            

            // 1. 0 can fill out the rest out buffer
            // 2. 
            if zero_len < (cap as u64){
                cap -= zero_len as usize;
                len += zero_len as usize;
                self.last_maxoff += zero_len;
                let buf_len = cmp::min(buf.len(), cap); 
                out[len..len + buf_len].copy_from_slice(&buf.data[..buf_len]);
                self.last_maxoff += buf_len as u64;
                if buf_len < buf.len(){
                    buf.consume(buf_len);

                    // We reached the maximum capacity, so end here.
                    break;
                }
                entry.remove();
            }else if zero_len == cap as u64 {
                self.last_maxoff += zero_len;
                // cap -= zero_len as usize;
                len += zero_len as usize;

                break;
            }else {
                self.last_maxoff += cap as u64;
                break;
            }
            
            // let buf_len = cmp::min(buf.len(), cap);

            // out[len..len + buf_len].copy_from_slice(&buf.data[..buf_len]);

            // self.off += buf_len as u64;

            // len += buf_len;
            // cap -= buf_len;

            // if buf_len < buf.len() {
            //     buf.consume(buf_len);

            //     // We reached the maximum capacity, so end here.
            //     break;
            // }

            // entry.remove();
        }


        Ok(len)
    }

    /// Resets the stream at the given offset.
    pub fn reset(&mut self, final_size: u64) -> Result<usize> {

        // Stream's known size is lower than data already received.
        if final_size < self.len {
            return Err(Error::FinalSize);
        }

        // Calculate how many bytes need to be removed from the connection flow
        // control.
        let max_data_delta = final_size - self.len;

        // Clear all data already buffered.
        self.off = final_size;

        self.data.clear();

       

        Ok(max_data_delta as usize)
    }


    /// Shuts down receiving data.
    pub fn shutdown(&mut self)  {
        self.data.clear();

    }

    /// Returns the lowest offset of data buffered.
    pub fn off_front(&self) -> u64 {
        self.off
    }


    /// Returns true if the stream has data to be read.
    fn ready(&self) -> bool {
        // let (_, buf) = match self.data.first_key_value() {
        //     Some(v) => v,
        //     None => return false,
        // };

        // buf.off() == self.off

        if self.data.is_empty(){
            return false
        }else {
            return true
        }
    }

    
}

/// Send-side stream buffer.
///
/// Stream data scheduled to be sent to the peer is buffered in a list of data
/// chunks ordered by offset in ascending order. Contiguous data can then be
/// read into a slice.
///
/// By default, new data is appended at the end of the stream, but data can be
/// inserted at the start of the buffer (this is to allow data that needs to be
/// retransmitted to be re-buffered).
#[derive(Debug, Default)]
pub struct SendBuf {
    /// Chunks of data to be sent, ordered by offset.
    data: VecDeque<RangeBuf>,

    // data:BTreeMap<u64, RangeBuf>,
    //retransmission buffer.
    // retran_data: VecDeque<RangeBuf>,

    /// The index of the buffer that needs to be sent next.
    pos: usize,

    /// The maximum offset of data buffered in the stream.
    off: u64,

    /// The amount of data currently buffered.
    len: u64,

    /// The maximum offset we are allowed to send to the peer.
    /// 
    /// max_data sent in one window, which is equal to congestion window size
    max_data: u64,

    /// The error code received via STOP_SENDING.
    error: Option<u64>,

    /// retransmission data in sendbuf
    used_length: usize,

    offset_recv: BTreeMap<u64, bool>,

    removed: u64,

    sent: usize,
}

impl SendBuf {
    /// Creates a new send buffer.
    /// max_data is equal to the congestion window size.
    fn new(max_data: u64) -> SendBuf {
        SendBuf {
            max_data,
            ..SendBuf::default()
        }
    }

    /// Returns the outgoing flow control capacity.
    pub fn cap(&mut self) -> Result<usize> {
        // The stream was stopped, so return the error code instead.
        if let Some(e) = self.error {
            return Err(Error::StreamStopped(e));
        }
        self.used_length = self.len();
        Ok((self.max_data - self.used_length as u64) as usize)
    }

    /// Inserts the given slice of data at the end of the buffer.
    ///
    /// The number of bytes that were actually stored in the buffer is returned
    /// (this may be lower than the size of the input buffer, in case of partial
    /// writes).
    /// write function is used to write new data into sendbuf, one congestion window 
    /// will run once.
    pub fn write(&mut self, mut data: &[u8], window_size: usize, off_len: usize, max_ack: u64) -> Result<usize> {
        self.recv_and_drop(max_ack);
        self.max_data = window_size as u64;
        self.removed = 0;
        self.sent = 0;
        // Get the stream send capacity. This will return an error if the stream
        // was stopped.
        self.len = self.len() as u64;
        self.used_length = self.len();
        //Addressing left data is greater than the window size
        if self.len >= window_size.try_into().unwrap(){
            return Ok(0);
        }

        if data.len() == 0{
            return  Ok(0);
        }
    
        let capacity = self.cap()?;
        // self.used_length = window_size;
        // self.used_length = 0;
        
        if data.len() > capacity {
            // Truncate the input buffer according to the stream's capacity.
            let len = capacity;
            data = &data[..len];

        }

        // We already recorded the final offset, so we can just discard the
        // empty buffer now.
        if data.is_empty() {
            return Ok(data.len());
        }

        let mut len = 0;

        /////
        if off_len > 0 {
            if data.len() > off_len{
            println!("data.len >> off_len");
            let first_buf: RangeBuf = RangeBuf::from(&data[..off_len], self.off);

            self.offset_recv.insert(self.off, true);

            self.data.push_back(first_buf);
            self.off += off_len as u64;
            self.len += off_len as u64;
            self.used_length += off_len;
            len += off_len;
            }else{
                println!("data.len << off_len");
                let first_buf: RangeBuf = RangeBuf::from(&data[..], self.off);
                self.offset_recv.insert(self.off, true);

                self.data.push_back(first_buf);
                self.off += data.len() as u64;
                self.len += data.len() as u64;
                self.used_length += data.len();
                len += data.len();
                return Ok(len);
            }

        }
        for chunk in data[off_len..].chunks(SEND_BUFFER_SIZE){

        // Split the remaining input data into consistently-sized buffers to
        // avoid fragmentation.
        //for chunk in data.chunks(SEND_BUFFER_SIZE) {
            
            len += chunk.len();          

            let buf = RangeBuf::from(chunk, self.off);
            
            // self.offset_index.insert( self.off,self.index);

            self.offset_recv.insert(self.off, true);

            // The new data can simply be appended at the end of the send buffer.
            self.data.push_back(buf);

            self.off += chunk.len() as u64;
            self.len += chunk.len() as u64;
            self.used_length += chunk.len();
        }
        Ok(len)
    }

    /// Returns the lowest offset of data buffered.
    pub fn off_front(&self) -> u64 {
        let mut pos = self.pos;

        // Skip empty buffers from the start of the queue.
        while let Some(b) = self.data.get(pos) {
            if !b.is_empty() {
                return b.off();
            }
            pos += 1;
        }

        self.off
    }

    /// Returns true if there is data to be written.
    fn ready(&self) -> bool {
        // !self.data.is_empty() && self.off_front() < self.off
        !self.data.is_empty()
    }

    /// Writes data from the send buffer into the given output buffer.
    pub fn emit(&mut self, out: &mut [u8]) -> Result<(usize,u64,bool)> {
        let mut stop = false;
        let mut out_len = out.len();
        if self.data.is_empty(){
            println!("no data");
        }

        let out_off = self.off_front();
     
        while out_len >= SEND_BUFFER_SIZE && self.ready() 
        {
            let buf = match self.data.get_mut(self.pos) {
                Some(v) => v,

                None => break,
            };

            if buf.is_empty() {
                self.pos += 1;
                continue;
            }

            let buf_len = cmp::min(buf.len(), out_len);
            let partial = buf_len <= buf.len();

            // Copy data to the output buffer.
            let out_pos:usize = 0;
            out[out_pos..out_pos + buf_len].copy_from_slice(&buf.data[..buf_len]);

            self.len -= buf_len as u64;
            self.used_length -= buf_len;

            out_len = buf_len;

            //buf.consume(buf_len);
            self.pos += 1;

            if partial {
                // We reached the maximum capacity, so end here.
                break;
            }

        }
        self.sent += out_len;

        //All data in the congestion control window has been sent. need to modify
        if self.sent  >= self.max_data.try_into().unwrap() {
            stop = true;
            self.pos = 0;
        }
        if self.pos == self.data.len(){
            stop = true;
            self.pos = 0;
        }
        Ok((out_len, out_off, stop))
    }

    /// Updates the max_data limit to the given value.
    pub fn update_max_data(&mut self, max_data: u64) {
        self.max_data = max_data;
    }

    // pub fn recv_and_drop(&mut self, ack_set: &[u64], unack_set: &[u64],len: usize) {
    pub fn recv_and_drop(&mut self, _max_ack: u64) {
        if self.data.is_empty() {
            return;
        }

        let recv_drop:Vec<bool> = self.offset_recv.values().cloned().collect();
        let mut iter = recv_drop.iter();
        self.data.retain(|_| *iter.next().unwrap());
        self.offset_recv.retain(|_, & mut v| v == true);
    }


    pub fn ack_and_drop(&mut self, offset:u64){
        if let Some(x) = self.offset_recv.get_mut(&offset) {
            *x = false;
        }
    }

    ////rewritetv
    /// Resets the stream at the current offset and clears all buffered data.
    pub fn reset(&mut self) -> Result<u64> {
        let unsent_off = self.off_front();
        let unsent_len = self.off_back() - unsent_off;

        // Drop all buffered data.
        self.data.clear();

        self.pos = 0;
        self.len = 0;
        self.off = unsent_off;

        Ok(unsent_len)
    }

    pub fn clear(&mut self){
        self.data.clear();
        self.pos = 0;
        self.off = 0;
    }

    /// Returns the largest offset of data buffered.
    pub fn off_back(&self) -> u64 {
        self.off
    }

    /// The maximum offset we are allowed to send to the peer.
    pub fn max_off(&self) -> u64 {
        self.max_data 
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    pub fn len(&self) -> usize{
        let mut length = 0;
        if self.data.is_empty(){
            return 0;
        }
        for item in self.data.iter(){
            length += item.data.len();
        }
        length
    }
    /// Returns true if the stream was stopped before completion.
    pub fn is_stopped(&self) -> bool {
        self.error.is_some()
    }

}



mod recovery;
mod packet;
// mod minmax;
use recovery::Recovery;

pub use crate::recovery::CongestionControlAlgorithm;
pub use crate::packet::Header;
pub use crate::packet::Type;
#[cfg(feature = "ffi")]
mod ffi;


