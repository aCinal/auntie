use alloc::{
    boxed::Box,
    slice,
    vec::Vec,
};
use core::ptr;
use crate::sighash::signature_hash;
use orchard::{
    Address,
    Anchor,
    builder::{Builder, BundleType, InProgress, PartiallyAuthorized},
    bundle::{Authorized, Bundle},
    circuit::ProvingKey,
    keys::{FullViewingKey, Scope, SpendAuthorizingKey, SpendingKey},
    Note,
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
use sgx::{
    println,
    rand::SgxRng,
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
// TODO: Remove pub(crate) once handling of blocks is implemented
pub(crate) type Transaction = Bundle<Authorized, ZatBalance>;

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

fn number_of_players() -> usize {
    // Resolve the number of players from the compiler's environment
    env!("AUNTIE_NUM_PLAYERS").parse().unwrap()
}

fn for_each_party<'a, T: 'a>(ptrs: *const T) -> impl Iterator<Item = &'a T> + Clone {
    unsafe { slice::from_raw_parts(ptrs, number_of_players() + 1).iter() }
}

fn ref_from_ptr_for_each_party<'a, T: 'a>(ptrs: *const *const T) -> impl Iterator<Item = &'a T> + Clone {
    for_each_party(ptrs).map(|&ptr| unsafe { ptr.as_ref() }.unwrap())
}

fn extract_input_notes<'a>(inputs: impl Iterator<Item = &'a Transaction>, advices: impl Iterator<Item = &'a Advice>) -> Option<Vec<Note>> {
    // Prepare a closure that maps a bundle to its unique note addressed to the recipient associated with a given advice
    let extract_unique_note = |(bundle, advice): (&Transaction, &Advice)| {
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
                println!("extract_input_notes: bundle has {n} notes addressed to the deposit address");
                None
            }
        }
    };
    // Extract notes from the input bundles (collect an iterator over Options into an Option<Vec<_>>)
    // - an entry was None (and now the the result is None) if there was zero notes or more than one
    // note matching the same advice
    inputs
        .zip(advices)
        .map(extract_unique_note)
        .collect()
}

fn parse_merkle_paths(merkle_paths: *const u8, merkle_paths_length: usize) -> Option<(Anchor, Vec<MerklePath>)> {
    // Assume the Merkle paths file has the format [anchor][pos0 as u32][path0][pos1 as u32][path1]...[posN as u32][pathN]
    const RAW_PATH_SIZE: usize = 32 * NOTE_COMMITMENT_TREE_DEPTH + 4;
    const RAW_ANCHOR_SIZE: usize = 32;

    let expected_size = RAW_ANCHOR_SIZE + RAW_PATH_SIZE * (number_of_players() + 1);
    if merkle_paths_length != expected_size {
        println!("parse_merkle_paths: Merkle paths size {} invalid, expected {}", merkle_paths_length, expected_size);
        return None
    }

    let merkle_paths = unsafe { slice::from_raw_parts(merkle_paths, merkle_paths_length) };
    // TODO: It would be nice we didn't panic here if bytes are not a canonical encoding of a pallas field element
    let anchor = MerkleHashOrchard::from_bytes(merkle_paths[..RAW_ANCHOR_SIZE].try_into().unwrap()).unwrap().into();
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
        })
        .collect();

    Some((anchor, merkle_paths))
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
    // Convert raw pointers into iterators over references
    let inputs = ref_from_ptr_for_each_party(inputs);
    let payouts = for_each_party(payouts);
    let payout_addresses = ref_from_ptr_for_each_party(payout_addresses);
    let advices = ref_from_ptr_for_each_party(advices);

    println!("zcash_create_transaction: extracting input notes...");
    // Map deposit transactions into input notes
    let inputs = match extract_input_notes(inputs.clone(), advices.clone()) {
        Some(notes) => notes,
        _ => return ptr::null_mut()
    };
    println!("zcash_create_transaction: found all input notes. Parsing Merkle paths...");

    let (anchor, merkle_paths) = match parse_merkle_paths(merkle_paths, merkle_paths_length) {
        Some((anchor, paths)) => (anchor, paths),
        _ => return ptr::null_mut()
    };

    let mut builder = Builder::new(BundleType::DEFAULT, anchor);

    println!("zcash_create_transaction: adding spends...");
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
                println!("zcash_create_transaction: failed to add spend with error {}", err);
            }
        });

    println!("zcash_create_transaction: adding outputs...");
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

    println!("zcash_create_transaction: building the bundle...");
    // Build the bundle
    let unauthorized = match builder.build(SgxRng) {
        Ok(Some((bundle, _))) => bundle,
        Err(err) => {
            println!("zcash_create_transaction: failed to build the bundle with error {}", err);
            return ptr::null_mut();
        },
        _ => {
            println!("zcash_create_transaction: failed to build the bundle");
            return ptr::null_mut();
        }
    };

    println!("zcash_create_transaction: proving the bundle...");
    let pk = ProvingKey::build();
    let mut rng = SgxRng;
    // Compute the signature hash of a transaction that has only this Orchard bundle
    let sighash = signature_hash(&unauthorized);
    let partially_authorized = match unauthorized.create_proof(&pk, &mut rng) {
        Ok(bundle) => bundle.prepare(rng, sighash.into()),
        Err(err) => {
            println!("zcash_create_transaction: failed to create the proof with error {}", err);
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
    unsafe { *sighash = signature_hash(transaction.as_ref().unwrap()); }
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
    // We need actual signatures, not just references to them, so clone each one
    let signatures: Vec<_> = ref_from_ptr_for_each_party(signatures)
        .map(|sig| sig.clone())
        .collect();

    // We take back ownership of unauthorized_transaction permanently
    let bundle = unsafe { Box::from_raw(unauthorized_transaction) };

    match bundle.append_signatures(&signatures).and_then(PartialTransaction::finalize) {
        Ok(authorized) => Box::into_raw(Box::new(authorized)),
        Err(e) => {
            println!("zcash_authorize_transaction: bundle.append_signatures() returned error {e}");
            ptr::null_mut()
        },
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_signature(signature: *mut Signature) {
    drop(unsafe{ Box::from_raw(signature) });
}
