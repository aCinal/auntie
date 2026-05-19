unsafe extern "C" {
    pub fn printf(ptr: *const i8, ...);
}

#[macro_export]
macro_rules! println {
    ($($arg:tt)*) => {{
        use alloc::format;
        let s = format!($($arg)*);
        // Ensure null-terminated string
        let mut bytes = s.into_bytes();
        bytes.push(0);
        unsafe { $crate::io::printf(b"%s\n\0".as_ptr() as *const i8, bytes.as_ptr()); }
    }};
}
