use core::ffi::{c_char, c_int, c_void};
use core::fmt;

unsafe extern "C" {
    fn sqlite3_fts5bigram_register(connection: *mut c_void, error: *mut *mut c_char) -> c_int;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RegisterError {
    code: i32,
}

impl RegisterError {
    pub const fn code(self) -> i32 {
        self.code
    }
}

impl fmt::Display for RegisterError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "could not register the unicode_bigram tokenizer (SQLite error {})",
            self.code
        )
    }
}

impl std::error::Error for RegisterError {}

/// Registers `unicode_bigram` on a raw `sqlite3*` connection.
///
/// # Safety
///
/// `connection` must be a valid, open `sqlite3*`. The connection must not be
/// used concurrently while registration runs, and must remain valid for all
/// subsequent use of the registered tokenizer.
pub unsafe fn register(connection: *mut c_void) -> Result<(), RegisterError> {
    let result = unsafe { sqlite3_fts5bigram_register(connection, core::ptr::null_mut()) };
    if result == 0 {
        Ok(())
    } else {
        Err(RegisterError { code: result })
    }
}
