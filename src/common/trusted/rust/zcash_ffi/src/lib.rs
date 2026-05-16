#![no_std]
extern crate alloc;

use rand_core::RngCore;
use sgx::rand::SgxRng;
use alloc::{
    boxed::Box,
    slice,
    vec::Vec,
};
use core::ptr;

unsafe extern "C" {
    fn printf(ptr: *const i8, ...);
}

#[macro_export]
macro_rules! format {
    ($($arg:tt)*) => {{
        use alloc::string::String;
        use core::fmt::Write;

        let mut s = String::new();
        write!(&mut s, $($arg)*).unwrap();
        s
    }};
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {{
        use core::ffi::c_char;

        let s = format!($($arg)*);

        // Ensure null-terminated string
        let mut bytes = s.into_bytes();
        bytes.push(0);

        unsafe {
            printf(b"%s\0".as_ptr() as *const c_char, bytes.as_ptr());
        }
    }};
}

use orchard::{
    Address,
    builder::{Builder, BundleType, InProgress, PartiallyAuthorized},
    bundle::{Authorized, Bundle},
    circuit::ProvingKey,
    keys::{FullViewingKey, Scope, SpendAuthorizingKey, SpendingKey},
    NOTE_COMMITMENT_TREE_DEPTH,
    note_encryption::OrchardDomain,
    primitives::redpallas,
    Proof,
    tree::{MerkleHashOrchard, MerklePath},
    value::NoteValue,
};

use pasta_curves::{
    group::ff::{Field, PrimeField},
    pallas,
};
use zcash_note_encryption::try_note_decryption;
use zcash_primitives::transaction::components::orchard::{
    read_v5_bundle,
    write_v5_bundle,
};
use zcash_protocol::value::ZatBalance;

#[derive(Debug)]
pub struct Advice {
    fvk: FullViewingKey,
    alpha: pallas::Scalar,
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_create_key() -> *mut SpendingKey {
    // Mirror internal "SpendingKey::random" method: note that if ask=0,
    // we should discard the corresponding sk
    let key = loop {
        let mut bytes = [0; 32];
        SgxRng.try_fill_bytes(&mut bytes).unwrap();
        let sk = SpendingKey::from_bytes(bytes);
        if sk.is_some().into() {
            break sk.unwrap();
        }
    };

    // Move the key to the heap and relinquish ownership via Box::into_raw
    Box::into_raw(Box::new(key))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_key(key: *mut SpendingKey) {
    drop(unsafe{ Box::from_raw(key) });
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
    drop(unsafe{ Box::from_raw(addr) });
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_create_advice(key: *const SpendingKey) -> *mut Advice {
    let fvk = FullViewingKey::from(unsafe { key.as_ref() }.unwrap());
    let alpha = pallas::Scalar::random(SgxRng);
    let advice = Advice {
        fvk,
        alpha,
    };
    Box::into_raw(Box::new(advice))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_advice(raw_advice: *const u8, raw_advice_length: usize) -> *mut Advice {
    let advice = match TryInto::<&[u8; 96+32]>::try_into(unsafe { slice::from_raw_parts(raw_advice, raw_advice_length) }) {
        Ok(slice) => Advice {
            fvk: FullViewingKey::from_bytes(slice[..96].try_into().unwrap()).unwrap(),
            alpha: pallas::Scalar::from_repr(slice[96..].try_into().unwrap()).unwrap(),
        },
        Err(_) => return ptr::null_mut(),
    };

    Box::into_raw(Box::new(advice))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_advice_impl(raw_advice: *mut [u8; 96+32], advice: *const Advice) {
    unsafe {
        let advice = advice.as_ref().unwrap();
        raw_advice.as_mut().unwrap()[..96].copy_from_slice(&advice.fvk.to_bytes());
        raw_advice.as_mut().unwrap()[96..].copy_from_slice(&advice.alpha.to_repr());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_advice(advice: *mut Advice) {
    drop(unsafe{ Box::from_raw(advice) });
}

type PartialTransaction = Bundle<InProgress<Proof, PartiallyAuthorized>, ZatBalance>;
type Transaction = Bundle<Authorized, ZatBalance>;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_transaction(raw_transaction: *const u8, raw_transaction_length: usize) -> *mut Transaction {
    let slice = unsafe { slice::from_raw_parts(raw_transaction, raw_transaction_length) };
    match read_v5_bundle(slice) {
        Ok(Some(bundle)) => Box::into_raw(Box::new(bundle)),
        _ => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_get_raw_transaction_length(transaction: *const Transaction) -> usize {
    let mut buffer: Vec<u8> = Vec::new();
    write_v5_bundle(unsafe { transaction.as_ref() }, &mut buffer).unwrap();
    buffer.len()
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_transaction_impl(raw_transaction: *mut u8, transaction: *const Transaction) {
    let mut buffer: Vec<u8> = Vec::new();
    write_v5_bundle(unsafe { transaction.as_ref() }, &mut buffer).unwrap();
    // The caller should have first called zcash_get_raw_transaction_length and allocated a sufficiently-sized buffer
    unsafe { ptr::copy_nonoverlapping(buffer.as_ptr(), raw_transaction, buffer.len()); }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_partial_transaction(transaction: *mut PartialTransaction) {
    drop(unsafe{ Box::from_raw(transaction) });
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_transaction(transaction: *mut Transaction) {
    drop(unsafe{ Box::from_raw(transaction) });
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_create_transaction(
    inputs: *const *const Transaction,
    payouts: *const u64,
    payout_addresses: *const *const Address,
    advices: *const *const Advice,
    merkle_paths: *const u8,
    merkle_paths_length: usize,
) -> *mut PartialTransaction {
    // Resolve the number of players from the compiler's environment
    let num_players: usize = env!("AUNTIE_NUM_PLAYERS").parse().unwrap();

    let payouts = unsafe { slice::from_raw_parts(payouts, num_players + 1) }.iter();
    let payout_addresses = unsafe { slice::from_raw_parts(payout_addresses, num_players + 1) }
        .iter()
        .map(|ptr| unsafe { ptr.as_ref().unwrap() } );
    let advices = unsafe { slice::from_raw_parts(advices, num_players + 1) }
        .iter()
        .map(|ptr| unsafe { ptr.as_ref().unwrap() } );
    // Extract notes from the input bundles
    let inputs = unsafe { slice::from_raw_parts(inputs, num_players + 1) }
        .iter()
        .map(|ptr| unsafe { ptr.as_ref().unwrap() })
        .zip(advices.clone())
        .map(|(bundle, advice)| {
            let ivk = advice.fvk.to_ivk(Scope::External).prepare();
            let notes: Vec<_> = bundle
                .actions()
                .iter()
                .filter_map(|action| {
                    let domain = OrchardDomain::for_action(action);
                    try_note_decryption(&domain, &ivk, action).map(|(note, _recipient, _memo)| note)
                })
                .collect();
            // Assert there is exactly one note that matches the advice, but fail gracefully so that
            // the operator can always get a refund in case players misbehave (this would be redundant
            // if we checked there was only one note in zcash_deposited_amount)
            match notes.len() {
                1 => Some(notes[0]),
                n => {
                    print!("zcash_create_transaction: bundle has {n} notes addressed to the deposit address\n");
                    None
                }
            }
        });
    // inputs is a vector of Options at this point where an entry is None if there was zero notes or more
    // than one note matching the advice
    let inputs: Option<Vec<_>> = inputs.into_iter().collect();
    if !inputs.is_some() {
        return ptr::null_mut();
    }
    print!("zcash_create_transaction: found all input notes. Parsing Merkle paths...\n");
    let inputs = inputs.unwrap();

    // Assume the Merkle paths file has the format [anchor][pos0 as u32][path0][pos1 as u32][path1]...[posN as u32][pathN]
    const RAW_PATH_SIZE: usize = 32 * NOTE_COMMITMENT_TREE_DEPTH + 4;
    const RAW_ANCHOR_SIZE: usize = 32;
    let expected_size = RAW_ANCHOR_SIZE + RAW_PATH_SIZE * (num_players + 1);
    if merkle_paths_length != expected_size {
        print!("zcash_create_transaction: Merkle paths size {} invalid, expected {}\n", merkle_paths_length, expected_size);
        return ptr::null_mut();
    }
    let merkle_paths = unsafe { slice::from_raw_parts(merkle_paths, merkle_paths_length) };
    // TODO: It would be nice we didn't panic here if bytes are not a canonical encoding of a pallas field element
    let anchor = MerkleHashOrchard::from_bytes(merkle_paths[..RAW_ANCHOR_SIZE].try_into().unwrap()).unwrap();
    let merkle_paths = merkle_paths[RAW_ANCHOR_SIZE..]
        .chunks_exact(RAW_PATH_SIZE)
        .map(|chunk| {
            MerklePath::from_parts(
                u32::from_le_bytes(chunk[..4].try_into().unwrap()),
                // TODO: It would be nice we didn't panic here if bytes are not a canonical encoding of a pallas field element
                chunk[4..]
                    .chunks_exact(32)
                    .map(|node| MerkleHashOrchard::from_bytes(node.try_into().unwrap()).unwrap())
                    .collect::<Vec<_>>()
                    .try_into()
                    .unwrap(),
            )
        });

    let mut builder = Builder::new(BundleType::DEFAULT, anchor.into());

    print!("zcash_create_transaction: adding spends...\n");

    // Add spends
    inputs
        .into_iter()
        .zip(advices)
        .zip(merkle_paths)
        .for_each(|((input, advice), merkle_path)| {
            if let Err(err) = builder.add_spend_with_alpha(
                advice.fvk.clone(),
                input,
                merkle_path,
                Some(advice.alpha),
            ) {
                print!("builder.add_spend returned error {}\n", err);
            }
        });

    print!("zcash_create_transaction: adding outputs...\n");

    // Add outputs
    payouts
        .zip(payout_addresses)
        .for_each(|(payout, recipient)| {
            // We do not check for payout > 0 as we would be adding dummy outputs anyway to not leak any information
            // about contract result
            builder.add_output(
                None,
                recipient.clone(),
                NoteValue::from_raw(*payout),
                [0u8; 512],
            ).unwrap()
        });

    print!("zcash_create_transaction: building the bundle...\n");

    // Build the bundle
    let unauthorized = match builder.build(SgxRng) {
        Ok(Some((bundle, _))) => bundle,
        _ => {
            print!("zcash_create_transaction: failed to build the bundle\n");
            return ptr::null_mut();
        },
    };

    print!("zcash_create_transaction: proving the bundle...\n");

    let pk = ProvingKey::build();
    let mut rng = SgxRng;
    // TODO: Use correct SIGHASH (of the whole transaction?)
    let sighash = unauthorized.commitment();
    let partially_authorized = match unauthorized.create_proof(&pk, &mut rng) {
        Ok(bundle) => bundle.prepare(rng, sighash.into()),
        _ => {
            print!("zcash_create_transaction: failed to create the proof\n");
            return ptr::null_mut();
        },
    };

    Box::into_raw(Box::new(partially_authorized))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_deposited_amount(transaction: *const Transaction, key: *const SpendingKey) -> u64 {
    let fvk = FullViewingKey::from(unsafe { key.as_ref() }.unwrap());
    let ivk = fvk.to_ivk(Scope::External).prepare();
    let bundle = unsafe { transaction.as_ref() }.unwrap();
    bundle
        .actions()
        .iter()
        .filter_map(|action| {
            let domain = OrchardDomain::for_action(action);
            try_note_decryption(&domain, &ivk, action).map(|(note, _recipient, _memo)| note)
        })
        .map(|note| note.value().inner())
        .sum()
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_hash_transaction(sighash: *mut [u8; 32], transaction: *const PartialTransaction) {
    unsafe { *sighash = transaction.as_ref().unwrap().commitment().into(); }
}

type Signature = redpallas::Signature<redpallas::SpendAuth>;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_sign_transaction(key: *const SpendingKey, advice: *const Advice, sighash: *const [u8; 32]) -> *mut Signature {
    let ask: SpendAuthorizingKey = unsafe{ key.as_ref() }.unwrap().into();
    let alpha = unsafe { advice.as_ref() }.unwrap().alpha;
    let signature = ask.randomize(&alpha).sign(SgxRng, unsafe { sighash.as_ref() }.unwrap());
    Box::into_raw(Box::new(signature))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_signature(raw_signature: *const u8, raw_signature_length: usize) -> *mut Signature {
    let signature = match TryInto::<&[u8; 64]>::try_into(unsafe { slice::from_raw_parts(raw_signature, raw_signature_length) }) {
        Ok(slice) => Signature::from(*slice),
        Err(_) => return ptr::null_mut(),
    };
    Box::into_raw(Box::new(signature))
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_signature_impl(raw_signature: *mut [u8; 64], signature: *const Signature) {
    unsafe { *raw_signature = signature.as_ref().unwrap().into(); }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_authorize_transaction(unauthorized_transaction: *mut PartialTransaction, signatures: *const *const Signature) -> *mut Transaction {
    // Resolve the number of players from the compiler's environment
    let num_players: usize = env!("AUNTIE_NUM_PLAYERS").parse().unwrap();

    let signatures: Vec<_> = unsafe { slice::from_raw_parts(signatures, num_players + 1) }
        .iter()
        .map(|ptr| unsafe { ptr.as_ref().unwrap().clone() } )
        .collect();

    // We take back ownership of unauthorized_transaction permanently
    let bundle = unsafe { Box::from_raw(unauthorized_transaction) };

    match bundle.append_signatures(&signatures).and_then(PartialTransaction::finalize) {
        Ok(authorized) => Box::into_raw(Box::new(authorized)),
        Err(e) => {
            print!("zcash_authorize_transaction: bundle.append_signatures() returned error {e}\n");
            ptr::null_mut()
        },
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_signature(signature: *mut Signature) {
    drop(unsafe{ Box::from_raw(signature) });
}

// TODO: Use actual chain of blocks here
type Blocks = Transaction;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_blocks(blocks: *const u8, blocks_length: usize) -> *mut Blocks {
    // At the moment, we do not support timeout or delay of settlement and expect to just see the settlement
    // transaction before we release the payout key and functionality output
    zcash_import_transaction(blocks, blocks_length)
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_blocks(blocks: *mut Blocks) {
    zcash_release_transaction(blocks)
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_authorized_and_buried(sighash: *const [u8; 32], _deposit_key: *const SpendingKey, blocks: *const Blocks) -> i32 {
    // TODO: Look for the transaction in a time-independent manner (always taking O(|blocks|) time and not branching) - there is no reason to leak that the
    //       settlement transaction is there early (even though it's technically fine: the players will not be able to issue informed bribes anyway)

    // Note that we do not actually need to look at the bundle at all, we only need to check that we got the preimage of the sighash
    // that we obtained from the operator's TEE and signed. If it is in any way invalid, it could not have been accepted to the blockchain
    // which means the operator is misbehaving and we are their accomplices! We may be able to obtain the functionality's output but in so
    // doing we forfeit the deposit, and the operator is also not redeeming their collateral.
    print!("zcash_authorized_and_buried stub called: checking only that the settlement transaction is valid as delay of settlement is not yet implemented\n");

    let actual: [u8; 32] = unsafe { blocks.as_ref() }.unwrap().commitment().into();
    if unsafe { *sighash } == actual {
        env!("AUNTIE_SETTLE_DELAY_BLOCKS").parse().unwrap()
    } else {
        0
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_blocks_since_checkpoint(_blocks: *const Blocks) -> i32 {
    print!("zcash_blocks_since_checkpoint returning 0, as contract timeout is not yet implemented\n");
    0
}
