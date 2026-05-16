use rand_core::{CryptoRng, RngCore};

unsafe extern "C" {
    fn sgx_read_rand(rand: *mut u8, length_in_bytes: usize) -> u32;
}

pub struct SgxRng;

fn sgx_fill(buf: &mut [u8]) {
    let ret = unsafe { sgx_read_rand(buf.as_mut_ptr(), buf.len()) };
    if ret != 0 {
        panic!("sgx_read_rand failed!");
    }
}

impl RngCore for SgxRng {
    fn next_u32(&mut self) -> u32 {
        let mut buf = [0u8; 4];
        sgx_fill(&mut buf);
        u32::from_ne_bytes(buf)
    }

    fn next_u64(&mut self) -> u64 {
        let mut buf = [0u8; 8];
        sgx_fill(&mut buf);
        u64::from_ne_bytes(buf)
    }

    fn fill_bytes(&mut self, dst: &mut [u8]) {
        sgx_fill(dst);
    }

    fn try_fill_bytes(&mut self, dst: &mut [u8]) -> Result<(), rand_core::Error> {
        sgx_fill(dst);
        Ok(())
    }
}

impl CryptoRng for SgxRng {}
