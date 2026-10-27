//! Services portion of the Slippi port. This does not emulate EXI, perform
//! rollback, own the game's ENet socket, or prove that Melee is online-ready.
//! Uses upstream's protocols and TLS implementation without a custom exchange.
use std::ffi::{CStr, CString, c_char};
use std::path::PathBuf;
use slippi_gg_api::APIClient;
use slippi_matchmaking::{MatchRequest, MatchmakingClient};
use slippi_user::UserManager;

pub struct Services {
    user: UserManager,
    matchmaking: MatchmakingClient,
}

impl Services {
    fn new(folder: PathBuf, version: String) -> Self {
        let user = UserManager::new(APIClient::new(&version), folder, version.clone());
        let matchmaking = MatchmakingClient::new(user.clone(), version);
        Self { user, matchmaking }
    }
}

impl Drop for Services {
    fn drop(&mut self) { self.matchmaking.cancel(); }
}

/// Caller passes UTF-8, NUL-terminated strings; the returned opaque object must
/// remain alive until all calls have finished. No global logger is installed:
/// account content and backend responses must not enter Porpoise diagnostics.
/// # Safety
/// Both pointers must address valid strings for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_services_create(folder: *const c_char, version: *const c_char) -> *mut Services {
    if folder.is_null() || version.is_null() { return std::ptr::null_mut(); }
    let Ok(folder) = (unsafe { CStr::from_ptr(folder) }).to_str() else { return std::ptr::null_mut(); };
    let Ok(version) = (unsafe { CStr::from_ptr(version) }).to_str() else { return std::ptr::null_mut(); };
    if folder.is_empty() || version.is_empty() { return std::ptr::null_mut(); }
    let services = Services::new(PathBuf::from(folder), version.into());
    // Upstream supports a local-development URL environment override. Never
    // send imported accounts to an override endpoint through this adapter.
    if services.matchmaking.url() != "wss://matchmaking.slippi.gg/v1/queue" {
        return std::ptr::null_mut();
    }
    Box::into_raw(Box::new(services))
}

/// Calls the actual upstream attempt_login on the caller's thread (may block
/// for HTTP). True means loaded, NOT that matchmaking accepted the playKey.
/// # Safety
/// `services` must be a live pointer returned by create; do not destroy concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_account_load(services: *const Services) -> bool {
    let Some(services) = (unsafe { services.as_ref() }) else { return false; };
    services.user.attempt_login() && services.user.is_logged_in()
}

/// Cancel discovery and invoke upstream logout (also removes its user.json).
/// # Safety
/// `services` must be live or null. No caller may continue a previous session.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_account_logout(services: *mut Services) {
    if let Some(s) = unsafe { services.as_mut() } {
        s.matchmaking.cancel();
        s.user.logout();
    }
}

/// Begin actual upstream Unranked (1) or Direct (2) search. The core MUST first
/// bind its real ENet socket and pass its nonzero port. Direct code bytes are
/// upstream's Shift-JIS format, not UTF-8. NAT candidates must later be supplied
/// from STUN on that same socket; this initial adapter supplies no candidates.
/// Not called by the launcher while EXI/rollback/peer netplay are unavailable.
/// # Safety
/// `services` must be live. `code` must address `length` readable bytes when length > 0.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_search_begin(
    services: *const Services, mode: u8, port: u16, code: *const u8, length: usize,
) -> u64 {
    let Some(services) = (unsafe { services.as_ref() }) else { return 0; };
    if !services.user.is_logged_in() || !matches!(mode, 1 | 2) || port == 0 || length > 64 {
        return 0;
    }
    let bytes = if length == 0 { &[][..] } else {
        if code.is_null() { return 0; }
        unsafe { std::slice::from_raw_parts(code, length) }
    };
    if mode == 2 && bytes.is_empty() { return 0; }
    services.matchmaking.start(MatchRequest {
        mode, netplay_port: port, connect_code: bytes.to_vec(), ..Default::default()
    })
}

/// Upstream states: 0 idle, 1 connecting, 2 queued, 3 matched, 4 failed.
/// Matched means discovery succeeded; it is not a connected rollback session.
/// # Safety
/// `services` must be live or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_search_state(services: *const Services) -> i32 {
    unsafe { services.as_ref() }.map_or(-1, |s| s.matchmaking.state() as i32)
}

/// # Safety
/// `services` must be live or null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_search_cancel(services: *const Services, session: u64) {
    if let Some(s) = unsafe { services.as_ref() } { s.matchmaking.cancel_session(session); }
}

/// Takes upstream's actual match response once. This internal peer/session
/// payload must never be logged or put on a diagnostic screen. The caller
/// parses it using the matching upstream SlippiNetplay implementation.
/// # Safety
/// `services` must be live or null; free a nonnull result with pp_slippi_string_free.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_search_take_result(services: *const Services) -> *mut c_char {
    let Some(s) = (unsafe { services.as_ref() }) else { return std::ptr::null_mut(); };
    s.matchmaking.take_result_json().and_then(|text| CString::new(text).ok())
        .map_or(std::ptr::null_mut(), CString::into_raw)
}

/// # Safety
/// `text` must be null or a result returned by search_take_result, freed exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_string_free(text: *mut c_char) {
    if !text.is_null() { drop(unsafe { CString::from_raw(text) }); }
}

/// # Safety
/// May be called once for a create result, after every user of it has stopped.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pp_slippi_services_destroy(services: *mut Services) {
    if !services.is_null() { drop(unsafe { Box::from_raw(services) }); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn idle_and_cancel_need_no_network() {
        let s = Services::new(PathBuf::from("/nonexistent-porpoise-synthetic-test"), "3.0.0".into());
        assert!(!s.user.is_logged_in());
        assert_eq!(s.matchmaking.state() as u8, 0);
        s.matchmaking.cancel();
        assert_eq!(s.matchmaking.state() as u8, 0);
    }
    #[test]
    fn ffi_rejects_missing_objects() {
        unsafe {
            assert!(pp_slippi_services_create(std::ptr::null(), std::ptr::null()).is_null());
            assert!(!pp_slippi_account_load(std::ptr::null()));
            assert_eq!(pp_slippi_search_state(std::ptr::null()), -1);
            assert_eq!(pp_slippi_search_begin(std::ptr::null(), 1, 1234, std::ptr::null(), 0), 0);
            pp_slippi_services_destroy(std::ptr::null_mut());
        }
    }
}
