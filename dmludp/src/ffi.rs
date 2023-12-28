use std::ffi;
use std::ptr;
use std::slice;

use std::net::Ipv4Addr;
use std::net::Ipv6Addr;
use std::net::SocketAddr;
use std::net::SocketAddrV4;
use std::net::SocketAddrV6;

use libc::c_char;
use libc::c_int;
use libc::c_void;
use libc::size_t;
use libc::sockaddr;
use libc::ssize_t;
use libc::timespec;
#[cfg(not(windows))]
use libc::AF_INET;
#[cfg(windows)]
use winapi::shared::ws2def::AF_INET;

#[cfg(not(windows))]
use libc::AF_INET6;
#[cfg(windows)]
use winapi::shared::ws2def::AF_INET6;

#[cfg(not(windows))]
use libc::in_addr;
#[cfg(windows)]
use winapi::shared::inaddr::IN_ADDR as in_addr;

#[cfg(not(windows))]
use libc::in6_addr;
#[cfg(windows)]
use winapi::shared::in6addr::IN6_ADDR as in6_addr;

#[cfg(not(windows))]
use libc::sa_family_t;
#[cfg(windows)]
use winapi::shared::ws2def::ADDRESS_FAMILY as sa_family_t;

#[cfg(not(windows))]
use libc::sockaddr_in;
#[cfg(windows)]
use winapi::shared::ws2def::SOCKADDR_IN as sockaddr_in;

#[cfg(not(windows))]
use libc::sockaddr_in6;
#[cfg(windows)]
use winapi::shared::ws2ipdef::SOCKADDR_IN6_LH as sockaddr_in6;

#[cfg(not(windows))]
use libc::sockaddr_storage;
#[cfg(windows)]
use winapi::shared::ws2def::SOCKADDR_STORAGE_LH as sockaddr_storage;

#[cfg(windows)]
use libc::c_int as socklen_t;
#[cfg(not(windows))]
use libc::socklen_t;

#[cfg(windows)]
use winapi::shared::in6addr::in6_addr_u;
#[cfg(windows)]
use winapi::shared::inaddr::in_addr_S_un;
#[cfg(windows)]
use winapi::shared::ws2ipdef::SOCKADDR_IN6_LH_u;

use crate::*;

#[no_mangle]
pub extern "C" fn dmludp_config_new() -> *mut Config {
    match Config::new() {
        Ok(c) => Box::into_raw(Box::new(c)),

        Err(_) => ptr::null_mut(),
    }
}



#[no_mangle]
pub extern "C" fn dmludp_config_set_cc_algorithm_name(
    config: &mut Config, name: *const c_char,
) -> c_int {
    let name = unsafe { ffi::CStr::from_ptr(name).to_str().unwrap() };
    match config.set_cc_algorithm_name(name) {
        Ok(_) => 0,

        Err(e) => e.to_c() as c_int,
    }
}

#[no_mangle]
pub extern "C" fn dmludp_config_set_cc_algorithm(
    config: &mut Config, algo: CongestionControlAlgorithm,
) {
    config.set_cc_algorithm(algo);
}



#[no_mangle]
pub extern "C" fn dmludp_config_free(config: *mut Config) {
    unsafe { let _ = Box::from_raw(config); };
}

#[no_mangle]
pub extern "C" fn dmludp_header_info(
    buf: *mut u8, buf_len: size_t, _ty: *mut i32, pn: *mut i32
) -> c_int {
    let buf = unsafe { slice::from_raw_parts_mut(buf, buf_len) };
    let hdr = match Header::from_slice(buf) {
        Ok(v) => v,

        Err(e) => return e.to_c() as c_int,
    };

    unsafe {
        *pn = hdr.pkt_num as i32;

    }
    let mut result = 0;
    if hdr.ty == Type::Retry{
        result = 1;
    }
    if hdr.ty == Type::Handshake{
        result = 2;
    }
    if hdr.ty == Type::Application{
        result = 3;
    }
    if hdr.ty == Type::ElictAck{
        result = 4;
    }
    if hdr.ty == Type::ACK{
        result = 5;
    }
    if hdr.ty == Type::Stop{
        result = 6;
    }
    if hdr.ty == Type::Fin{
        result = 7;
    }
    if hdr.ty == Type::StartAck{
        result = 8;
    }
    result
    // 0
}


#[no_mangle] 
pub extern "C" fn dmludp_accept(
    local: &sockaddr, local_len: socklen_t, peer: &sockaddr, peer_len: socklen_t,
    config: &mut Config,
) -> *mut Connection {

    let local = std_addr_from_c(local, local_len);
    let peer = std_addr_from_c(peer, peer_len);

    match accept(local, peer, config) {
        Ok(c) => Box::into_raw(Box::new(c)),

        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn dmludp_connect(
    local: &sockaddr, local_len: socklen_t, peer: &sockaddr, peer_len: socklen_t,
    config: &mut Config,
) -> *mut Connection {
    
    let local = std_addr_from_c(local, local_len);
    let peer = std_addr_from_c(peer, peer_len);

    match connect(local, peer, config) {
        Ok(c) => Box::into_raw(Box::new(c)),

        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn dmludp_set_rtt(conn: &mut Connection, interval: i64){
    conn.set_rtt(interval);
}


#[no_mangle]
pub extern "C" fn dmludp_data_write(conn: &mut Connection, buf:* const u8, len: size_t){
    if len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let data_slice = unsafe { std::slice::from_raw_parts(buf, len) };
    conn.data_write(data_slice);
}

// data from dmludp to application
#[no_mangle]
pub extern "C" fn dmludp_data_read(conn: &mut Connection, buf:* mut u8, len: size_t)->ssize_t{
    if len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let data_slice = unsafe { std::slice::from_raw_parts_mut(buf, len) };
    conn.read(data_slice).try_into().unwrap()
}

#[no_mangle]
pub extern "C" fn dmludp_conn_send_all(
    conn: &mut Connection,
) -> bool {
    conn.send_all() 
}


#[repr(C)]
pub struct RecvInfo<'a> {
    from: &'a sockaddr,
    from_len: socklen_t,
    to: &'a sockaddr,
    to_len: socklen_t,
}

impl<'a> From<&RecvInfo<'a>> for crate::RecvInfo {
    fn from(info: &RecvInfo) -> crate::RecvInfo {
        crate::RecvInfo {
            from: std_addr_from_c(info.from, info.from_len),
            to: std_addr_from_c(info.to, info.to_len),
        }
    }
}

#[no_mangle]
pub extern "C" fn dmludp_conn_recv(
    conn: &mut Connection, buf: *mut u8, buf_len: size_t) -> ssize_t {
    if buf_len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let buf = unsafe { slice::from_raw_parts_mut(buf, buf_len) };

    match conn.recv_slice(buf) {
        Ok(v) => v as ssize_t,

        Err(e) => e.to_c(),

    }
}

#[no_mangle]
pub extern "C" fn dmludp_conn_is_empty(conn: &mut Connection) -> bool{
    conn.data_is_empty()
}

#[no_mangle]
pub extern "C" fn dmludp_is_waiting(conn: &mut Connection) -> bool{
    conn.waiting()
}

#[no_mangle]
pub extern "C" fn dmludp_buffer_is_empty(conn: &mut Connection) -> bool{
    conn.is_empty()
}

#[no_mangle]
pub extern "C" fn dmludp_is_empty(conn: &mut Connection) -> bool{
    conn.empty()
}

#[no_mangle]
pub extern "C" fn dmludp_conn_is_stop(conn: &mut Connection) -> bool{
    conn.is_stopped()
}

#[repr(C)]
pub struct SendInfo {
    from: sockaddr_storage,
    from_len: socklen_t,
    to: sockaddr_storage,
    to_len: socklen_t,
}

///modified later
#[no_mangle]
pub extern "C" fn dmludp_conn_send(
    conn: &mut Connection, out: *mut u8, out_len: size_t, out_info: &mut SendInfo,
) -> ssize_t {
    if out_len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let out = unsafe { slice::from_raw_parts_mut(out, out_len) };

    match conn.send_data(out) {
        Ok((v, info)) => {
            out_info.from_len = std_addr_to_c(&info.from, &mut out_info.from);
            out_info.to_len = std_addr_to_c(&info.to, &mut out_info.to);

            v as ssize_t
        },

        Err(e) => e.to_c(),
    }
}

#[no_mangle]
pub extern "C" fn dmludp_send_data_stop(conn: &mut Connection,out: *mut u8, out_len: size_t,
)->ssize_t{
    if out_len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let out = unsafe { slice::from_raw_parts_mut(out, out_len) };
    match conn.send_data_stop(out) {
        Ok(v) => {
            v as ssize_t
        },

        Err(e) => e.to_c(),
    }
}

#[no_mangle]
pub extern "C" fn dmludp_send_data_handshake(conn: &mut Connection,out: *mut u8, out_len: size_t)->ssize_t{
    if out_len > <ssize_t>::max_value() as usize {
        panic!("The provided buffer is too large");
    }

    let out = unsafe { slice::from_raw_parts_mut(out, out_len) };
    match conn.send_data_handshake(out) {
        Ok(v) => {
            v as ssize_t
        },

        Err(e) => e.to_c(),
    }
}

#[no_mangle]
pub extern "C" fn dmludp_get_rtt(conn: &mut Connection) -> f64{
    conn.get_rtt()
}

struct AppData(*mut c_void);
unsafe impl Send for AppData {}
unsafe impl Sync for AppData {}


#[no_mangle]
pub extern "C" fn dmludp_conn_is_closed(conn: &Connection) -> bool {
    conn.is_closed()
}


#[no_mangle]
pub extern "C" fn dmludp_conn_free(conn: *mut Connection) {
    unsafe { let _ = Box::from_raw(conn); };
}


fn std_addr_from_c(addr: &sockaddr, addr_len: socklen_t) -> SocketAddr {
    match addr.sa_family as i32 {
        AF_INET => {
            assert!(addr_len as usize == std::mem::size_of::<sockaddr_in>());

            let in4 = unsafe { *(addr as *const _ as *const sockaddr_in) };

            #[cfg(not(windows))]
            let ip_addr = Ipv4Addr::from(u32::from_be(in4.sin_addr.s_addr));
            #[cfg(windows)]
            let ip_addr = {
                let ip_bytes = unsafe { in4.sin_addr.S_un.S_un_b() };

                Ipv4Addr::from([
                    ip_bytes.s_b1,
                    ip_bytes.s_b2,
                    ip_bytes.s_b3,
                    ip_bytes.s_b4,
                ])
            };

            let port = u16::from_be(in4.sin_port);

            let out = SocketAddrV4::new(ip_addr, port);

            out.into()
        },

        AF_INET6 => {
            assert!(addr_len as usize == std::mem::size_of::<sockaddr_in6>());

            let in6 = unsafe { *(addr as *const _ as *const sockaddr_in6) };

            let ip_addr = Ipv6Addr::from(
                #[cfg(not(windows))]
                in6.sin6_addr.s6_addr,
                #[cfg(windows)]
                *unsafe { in6.sin6_addr.u.Byte() },
            );

            let port = u16::from_be(in6.sin6_port);

            #[cfg(not(windows))]
            let scope_id = in6.sin6_scope_id;
            #[cfg(windows)]
            let scope_id = unsafe { *in6.u.sin6_scope_id() };

            let out =
                SocketAddrV6::new(ip_addr, port, in6.sin6_flowinfo, scope_id);

            out.into()
        },

        _ => unimplemented!("unsupported address type"),
    }
}

fn std_addr_to_c(addr: &SocketAddr, out: &mut sockaddr_storage) -> socklen_t {
    let sin_port = addr.port().to_be();

    match addr {
        SocketAddr::V4(addr) => unsafe {
            let sa_len = std::mem::size_of::<sockaddr_in>();
            let out_in = out as *mut _ as *mut sockaddr_in;

            let s_addr = u32::from_ne_bytes(addr.ip().octets());

            #[cfg(not(windows))]
            let sin_addr = in_addr { s_addr };
            #[cfg(windows)]
            let sin_addr = {
                let mut s_un = std::mem::zeroed::<in_addr_S_un>();
                *s_un.S_addr_mut() = s_addr;
                in_addr { S_un: s_un }
            };

            *out_in = sockaddr_in {
                sin_family: AF_INET as sa_family_t,

                sin_addr,

                #[cfg(any(
                    target_os = "macos",
                    target_os = "ios",
                    target_os = "watchos",
                    target_os = "freebsd",
                    target_os = "dragonfly",
                    target_os = "openbsd",
                    target_os = "netbsd"
                ))]
                sin_len: sa_len as u8,

                sin_port,

                sin_zero: std::mem::zeroed(),
            };

            sa_len as socklen_t
        },

        SocketAddr::V6(addr) => unsafe {
            let sa_len = std::mem::size_of::<sockaddr_in6>();
            let out_in6 = out as *mut _ as *mut sockaddr_in6;

            #[cfg(not(windows))]
            let sin6_addr = in6_addr {
                s6_addr: addr.ip().octets(),
            };
            #[cfg(windows)]
            let sin6_addr = {
                let mut u = std::mem::zeroed::<in6_addr_u>();
                *u.Byte_mut() = addr.ip().octets();
                in6_addr { u }
            };

            #[cfg(windows)]
            let u = {
                let mut u = std::mem::zeroed::<SOCKADDR_IN6_LH_u>();
                *u.sin6_scope_id_mut() = addr.scope_id();
                u
            };

            *out_in6 = sockaddr_in6 {
                sin6_family: AF_INET6 as sa_family_t,

                sin6_addr,

                #[cfg(any(
                    target_os = "macos",
                    target_os = "ios",
                    target_os = "watchos",
                    target_os = "freebsd",
                    target_os = "dragonfly",
                    target_os = "openbsd",
                    target_os = "netbsd"
                ))]
                sin6_len: sa_len as u8,

                sin6_port: sin_port,

                sin6_flowinfo: addr.flowinfo(),

                #[cfg(not(windows))]
                sin6_scope_id: addr.scope_id(),
                #[cfg(windows)]
                u,
            };

            sa_len as socklen_t
        },
    }
}

#[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
fn std_time_to_c(time: &std::time::Instant, out: &mut timespec) {
    unsafe {
        ptr::copy_nonoverlapping(time as *const _ as *const timespec, out, 1)
    }
}
