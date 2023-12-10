use crate::Result;

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

#[derive(Clone, Default)]
// #[derive(Clone, Copy, Default)]
pub struct PktNumWindow {
    pub priority_record:std::collections::HashMap<u64,u64>,
    pub record:std::collections::HashMap<u64,[u64;2]>,
}
impl PktNumWindow {

}
