use alloc::{
    boxed::Box,
    slice,
};
use core::ptr;
use orchard::{
    Address,
    keys::{FullViewingKey, Scope, SpendingKey},
};
use rand_core::RngCore;
use sgx::rand::SgxRng;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_create_key() -> *mut SpendingKey {
    // Mirror internal "SpendingKey::random" method
    let key = loop {
        let mut bytes = [0; 32];
        SgxRng.try_fill_bytes(&mut bytes).unwrap();
        let sk = SpendingKey::from_bytes(bytes);
        if sk.is_some().into() {
            break sk.unwrap();
        }
    };
    Box::into_raw(Box::new(key))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_key(key: *mut SpendingKey) {
    drop(unsafe { Box::from_raw(key) });
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_derive_address(key: *const SpendingKey) -> *mut Address {
    let fvk = FullViewingKey::from(unsafe { key.as_ref() }.unwrap());
    // Set the diversifier index to 0 to obtain a default diversified payment address
    let addr = fvk.address_at(0u32, Scope::External);
    Box::into_raw(Box::new(addr))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_key_impl(raw_key: *mut [u8; 32], key: *const SpendingKey) {
    unsafe { *raw_key = *key.as_ref().unwrap().to_bytes(); }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_address(raw_address: *const u8, raw_address_length: usize) -> *mut Address {
    let addr = match unsafe { slice::from_raw_parts(raw_address, raw_address_length) }.try_into() {
        Ok(slice) => Address::from_raw_address_bytes(slice),
        Err(_) => return ptr::null_mut(),
    };
    if addr.is_some().into() {
        Box::into_raw(Box::new(addr.unwrap()))
    } else {
        ptr::null_mut()
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_address_impl(raw_address: *mut [u8; 43], addr: *const Address) {
    unsafe { *raw_address = addr.as_ref().unwrap().to_raw_address_bytes(); }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_address(addr: *mut Address) {
    drop(unsafe { Box::from_raw(addr) });
}
