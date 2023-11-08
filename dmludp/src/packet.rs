use crate::Result;

// const FORM_BIT: u8 = 0x03;
// const FORM_RETRY: u8 = 0x01;
// const FIXED_BIT: u8 = 0x40;
// const KEY_PHASE_BIT: u8 = 0x04;

// const TYPE_MASK: u8 = 0x30;
// const PKT_NUM_MASK: u8 = 0x03;

// pub const MAX_PKT_NUM_LEN: usize = 4;

// const SAMPLE_LEN: usize = 16;
// const SEND_BUFFER_SIZE:usize = 1024;

/// QUIC packet type.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Type {

    /// Retry packet.
    Retry = 0x01,

    /// Handshake packet.
    Handshake = 0x02,

    /// application packet.
    Application = 0x03,

    /// server ask reciver
    ElictAck = 0x04,

    ///ACK
    ACK = 0x05,

    /// STOP
    Stop = 0x06,

    /// Fin
    Fin = 0x07,

    // StartACK
    StartAck = 0x08,
}


/// A QUIC packet's header.
#[derive(Clone, PartialEq, Eq)]
pub struct Header {
    /// The type of the packet.
    pub ty: Type,

    pub pkt_num: u64,

    pub priority:u8,
    ///This offset is different from TCP offset. It refers to the last position in the 
    pub(crate) offset:u64,

    pub(crate) pkt_length:u64,

}


impl<'a> Header {
    /// Parses a QUIC packet header from the given buffer.
    ///
    /// The `dcid_len` parameter is the length of the destination connection ID,
    /// required to parse short header packets.
    ///
    /// ## Examples:
    ///
    /// ```no_run
    /// # const LOCAL_CONN_ID_LEN: usize = 16;
    /// # let mut buf = [0; 512];
    /// # let mut out = [0; 512];
    /// # let socket = std::net::UdpSocket::bind("127.0.0.1:0").unwrap();
    /// let (len, src) = socket.recv_from(&mut buf).unwrap();
    ///
    /// let hdr = quiche::Header::from_slice(&mut buf[..len], LOCAL_CONN_ID_LEN)?;
    /// # Ok::<(), quiche::Error>(())
    /// ```
    #[inline]
    pub fn from_slice<'b>(
        buf: &'b mut [u8], 
    ) -> Result<Header> {
        let mut b = octets::OctetsMut::with_slice(buf);
        Header::from_bytes(&mut b)
    }

    pub(crate) fn from_bytes<'b>(
        b: &'b mut octets::OctetsMut, 
    ) -> Result<Header> {
        let first = b.get_u8()?;

        let ty = if first == 0x01{
            Type::Retry
        }else if first == 0x02{
            Type::Handshake
        }else if first == 0x03{
            Type::Application
        }else if first == 0x04{
            Type::ElictAck
        }else if first == 0x05{
            Type::ACK
        }else if first == 0x06{
            Type::Stop
        }else if first == 0x07{
            Type::Fin
        }else{
            Type::StartAck
        };

        let second = b.get_u64()?;
        let third = b.get_u8()?;
        let forth = b.get_u64()?;
        let fifth = b.get_u64()?;

        //Packet handshake, elict_ack has no content.
        Ok(Header {
            ty:ty,
            pkt_num: second,
            priority: third,
            offset: forth,
            pkt_length: fifth,
        })
    }

    pub(crate) fn to_bytes(&self, out: &mut octets::OctetsMut) -> Result<()> {
        
        let first:u8 = if self.ty == Type::Retry{
            0x01 as u8
        }else if self.ty == Type::Handshake{
            0x02 as u8
        }else if self.ty == Type::Application{
            0x03 as u8
        }else if self.ty == Type::ElictAck{
            0x04 as u8
        }else if self.ty == Type::ACK{
            0x05 as u8
        }else if self.ty == Type::Stop{
            0x06 as u8
        }else{
            0x07 as u8
        };

        ////// no related data
        out.put_u8(first)?;
        out.put_u64(self.pkt_num)?;
        out.put_u8(self.priority)?;
        out.put_u64(self.offset)?;
        out.put_u64(self.pkt_length)?;

        Ok(())
    }

    /// Returns true if the packet has a long header.
    ///
    /// The `b` parameter represents the first byte of the QUIC header.
    // fn is_application(b: u8) -> bool {
    //     b & FORM_BIT != FORM_BIT
    // }

    // fn is_retry(b: u8) -> bool {
    //     b & FORM_RETRY != FORM_RETRY
    // }

    //Returning the length of the header
    pub fn len(&self)-> usize{
        26
    }
}
#[derive(Clone)]
pub struct PktNumSpace {

    pub next_pkt_num: u64,

    pub recv_pkt_num: PktNumWindow,

}

impl PktNumSpace {
    pub fn new() -> PktNumSpace {
        PktNumSpace {
            next_pkt_num: 0,

            recv_pkt_num: PktNumWindow::default(),

        }
    }


}
// #[derive(Clone, Default)]
// // #[derive(Clone, Copy, Default)]
// pub struct PktNumWindow {
 
//     pub record:std::collections::HashMap<u64,[u64;4]>,
// }

// impl PktNumWindow {
//     pub fn additem(&mut self, offset: u64, priority:usize, pkt_length:usize) {
//         let recived:usize = 1;
//         let retrans:usize = priority;
//         let record_pair:[u64;4] = [priority as u64,pkt_length as u64,retrans as u64 ,recived as u64];
//         self.record.insert(offset,record_pair);
//     }

//     pub fn insert_not(&mut self,offset:u64){
//         let recived:usize = 0;
//         let record_pair:[u64;4] = [3,0,2,recived as u64];
//         self.record.insert(offset,record_pair);
//     }

//     pub fn update_recived(&mut self, offset: u64){
//         let [pri, plength, mut retrans,_recived]= self.record[&offset];
//         if retrans > 0{
//             retrans -=1;
//         }

//         *self.record.get_mut(&offset).unwrap()  = [pri, plength, retrans,1];
//     }

//     fn get_length(&mut self, offset:&u64) -> u64 {
//         let [_pri, plength, mut _retrans,_recived]= self.record[&offset];
//         plength
//     }
// }
#[derive(Clone, Default)]
// #[derive(Clone, Copy, Default)]
pub struct PktNumWindow {
    pub priority_record:std::collections::HashMap<u64,u64>,
    pub record:std::collections::HashMap<u64,[u64;2]>,
}
impl PktNumWindow {
    //add item pair(offset, priority)
    pub fn additem(&mut self, offset: u64, priority:usize) {
        if offset%1024 == 0{
            self.priority_record.insert(offset,priority as u64);
        }   
        self.update_retrans(offset, priority.try_into().unwrap(), 1);
    }

    //
    // pub fn exist_or_not(&mut self, offset: u64, priority:usize){

    // }

    pub fn get_priority(&self, offset:u64)->u64{
        if self.priority_record.contains_key(&offset){
            *self.priority_record.get(&offset).unwrap()
        }else {
            0 as u64
        }
    }

    //check the received or not and update the retranmission times
    pub fn check_received(&mut self, offset: u64, sent: bool)->bool{
        if sent == false{
            if self.record.contains_key(&offset){
                let condition = self.record.get(&offset).unwrap();
                let received = condition[1];
                let retrans = condition[0];
                if received == 1{
                    return true
                }else {
                    if retrans > 0{
                        self.update_retrans(offset, retrans-1, received);
                    }else {
                        self.update_retrans(offset, 0, 1);
                    }
                    return false
                }
            }else {
                //
                let priority = self.get_priority(offset - offset%1024);
                if priority == 0{
                    self.additem(offset - offset%1024, 3);
                    self.update_retrans(offset, 2, 0);
                }else {
                    self.update_retrans(offset, priority-1, 0);
                }
                return false
            }
        }else{
            if self.record.contains_key(&offset){
                let condition = self.record.get(&offset).unwrap();
                let received = condition[1];
                let _retrans = condition[0];
                if received == 1{
                    return true
                }else {
                    return false
                }
            }else {
                return false
            }
        }
        
    }

    pub fn get_retrans_times(& self, offset: u64)->u64{
        if self.record.contains_key(&offset){
            let retrans = self.record.get(&offset).unwrap()[0];
            let recv = self.record.get(&offset).unwrap()[1];
            if recv == 1{
                return 0
            }else {
                return retrans+1
            }
        }else {
            return 0
        }
    }

    pub fn update_item(& mut self, offset: u64, priority:usize){
        if offset%1024 == 0{
            self.priority_record.insert(offset,priority as u64);
        }   
        self.update_retrans(offset, priority.try_into().unwrap(), 1);
    }

    pub fn update_retrans(&mut self, offset: u64, retrans: u64, received: u64){
        let new_condition:[u64;2] = [retrans, received];
        self.record.insert(offset, new_condition);
    }

    // pub fn update_recived(& mut self, offset: u64){
    //     if self.record.contains_key(&offset){
    //         self.record.insert(offset, [self.record.get(&offset).unwrap()[0],1]);
    //     }
    // }

    pub fn clear_record(& mut self){
        self.record.clear();
    }

}
