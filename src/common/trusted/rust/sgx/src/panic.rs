use core::panic::PanicInfo;

unsafe extern "C" {
    fn abort() -> !;
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_eh_personality() {
    unsafe {
        abort();
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe {
        abort();
    }
}
