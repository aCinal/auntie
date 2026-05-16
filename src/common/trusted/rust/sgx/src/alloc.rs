
use core::alloc::{GlobalAlloc, Layout};

unsafe extern "C" {
    fn malloc(size: usize) -> *mut u8;
    fn free(ptr: *mut u8);
}

pub struct SgxAllocator;

unsafe impl GlobalAlloc for SgxAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            // TODO: find out why sgx_aligned_malloc fails on second call and use that instead to respect layout.align()
            malloc(layout.size())
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        unsafe {
            // TODO: use sgx_aligned_free
            free(ptr);
        }
    }
}

#[global_allocator]
static GLOBAL: SgxAllocator = SgxAllocator;
