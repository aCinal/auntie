use alloc::{
    boxed::Box,
    slice,
    vec::Vec,
};
use core::ptr;
use crate::digests::{signature_digest, txid_digest};
use orchard::{
    Action,
    Address,
    Anchor,
    builder::{Builder, BundleType, InProgress, PartiallyAuthorized},
    bundle::{Authorized, Bundle, BundleVersion},
    circuit::{OrchardCircuitVersion, ProvingKey},
    keys::{FullViewingKey, Scope, SpendAuthorizingKey, SpendingKey},
    Note,
    NOTE_COMMITMENT_TREE_DEPTH,
    note_encryption::IronwoodDomain,
    primitives::redpallas,
    Proof,
    tree::{MerkleHashOrchard, MerklePath},
    value::NoteValue,
    ValuePool,
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
    read_v6_bundle,
    write_v6_bundle,
};
use zcash_protocol::{
    consensus::BranchId,
    value::ZatBalance,
};

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
    let slice = match TryInto::<&[u8; 96+32]>::try_into(unsafe { slice::from_raw_parts(raw_advice, raw_advice_length) }) {
        Ok(slice) => slice,
        Err(_) => return ptr::null_mut(),
    };

    // We do not really expect to have problems with decoding since advices are created within TEEs, but let us be consistent and vet all imports
    let fvk = FullViewingKey::from_bytes(slice[..96].try_into().unwrap());
    let alpha = pallas::Scalar::from_repr(slice[96..].try_into().unwrap());
    if fvk.is_some().into() && alpha.is_some().into() {
        let fvk = fvk.unwrap();
        let alpha = alpha.unwrap();
        let advice = Advice { fvk, alpha };
        Box::into_raw(Box::new(advice))
    } else {
        ptr::null_mut()
    }
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
    drop(unsafe { Box::from_raw(advice) });
}

type PartialTransaction = Bundle<InProgress<Proof, PartiallyAuthorized>, ZatBalance>;
type Transaction = Bundle<Authorized, ZatBalance>;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_import_transaction(raw_transaction: *const u8, raw_transaction_length: usize) -> *mut Transaction {
    let slice = unsafe { slice::from_raw_parts(raw_transaction, raw_transaction_length) };
    match read_v6_bundle(slice, BranchId::Nu6_3, ValuePool::Ironwood) {
        Ok(Some(bundle)) => Box::into_raw(Box::new(bundle)),
        _ => ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_get_raw_transaction_length(transaction: *const Transaction) -> usize {
    let mut buffer: Vec<u8> = Vec::new();
    write_v6_bundle(unsafe { transaction.as_ref() }, &mut buffer).unwrap();
    buffer.len()
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_export_transaction_impl(raw_transaction: *mut u8, transaction: *const Transaction) {
    let mut buffer: Vec<u8> = Vec::new();
    write_v6_bundle(unsafe { transaction.as_ref() }, &mut buffer).unwrap();
    // The caller should have first called zcash_get_raw_transaction_length and allocated a sufficiently-sized buffer
    unsafe { ptr::copy_nonoverlapping(buffer.as_ptr(), raw_transaction, buffer.len()); }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_partial_transaction(transaction: *mut PartialTransaction) {
    drop(unsafe { Box::from_raw(transaction) });
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_release_transaction(transaction: *mut Transaction) {
    drop(unsafe { Box::from_raw(transaction) });
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

fn parse_merkle_paths(merkle_paths: *const u8, merkle_paths_length: usize) -> Option<(Anchor, Vec<MerklePath>)> {
    // Assume the Merkle paths file has the format [anchor][pos0 as u32][path0][pos1 as u32][path1]...[posN as u32][pathN]
    const RAW_PATH_SIZE: usize = 32 * NOTE_COMMITMENT_TREE_DEPTH + 4;
    const RAW_ANCHOR_SIZE: usize = 32;

    let merkle_paths = unsafe { slice::from_raw_parts(merkle_paths, merkle_paths_length) };

    // Verify the size of the buffer
    if merkle_paths.len() < RAW_ANCHOR_SIZE {
        println!("parse_merkle_paths: buffer too short");
        return None
    }
    let paths_length = merkle_paths.len() - RAW_ANCHOR_SIZE;
    if paths_length % RAW_PATH_SIZE != 0 {
        println!("parse_merkle_paths: bad buffer, (length - 32) not divisible by path size");
        return None
    }

    let (anchor_bytes, paths_bytes) = merkle_paths.split_at(RAW_ANCHOR_SIZE);

    // Parse the anchor (one for all spends)
    let anchor = MerkleHashOrchard::from_bytes(anchor_bytes.try_into().unwrap());
    if anchor.is_none().into() {
        println!("parse_merkle_paths: non-canonical encoding of the anchor");
        return None
    }
    let anchor = anchor.unwrap().into();

    let mut paths = Vec::with_capacity(paths_length / RAW_PATH_SIZE);
    // Parse the Merkle paths (one for each spend)
    for chunk in paths_bytes.chunks_exact(RAW_PATH_SIZE) {
        let (position_bytes, path_bytes) = chunk.split_at(4);
        let position = u32::from_le_bytes(position_bytes.try_into().unwrap());

        let mut path = Vec::with_capacity(NOTE_COMMITMENT_TREE_DEPTH);
        for path_node in path_bytes.chunks_exact(32) {
            let node = MerkleHashOrchard::from_bytes(path_node.try_into().unwrap());
            if node.is_none().into() {
                println!("parse_merkle_path: non-canonical encoding of a path node");
                return None
            }
            path.push(node.unwrap())
        }

        paths.push(MerklePath::from_parts(position, path.try_into().unwrap()));
    }

    Some((anchor, paths))
}

fn extract_unique_note<'a, A: 'a>(actions: impl Iterator<Item = &'a Action<A>>, paths: impl Iterator <Item = MerklePath>, advice: &Advice) -> Option<(Note, MerklePath)> {
    // Prepare the decryption key
    let ivk = advice.fvk.to_ivk(Scope::External).prepare();
    let note_path_pairs: Vec<_> = actions
        .zip(paths)
        .filter_map(|(action, path)| {
            let domain = IronwoodDomain::for_action(action);
            try_note_decryption(&domain, &ivk, action)
                .map(|(note, _recipient, _memo)| (note, path.clone()))
        })
        .collect();
    // Assert there is exactly one note that matches the advice, but fail gracefully so that
    // the operator can always get a refund in case players misbehave (this would be redundant
    // if we checked there was only one note in zcash_deposited_amount)
    match note_path_pairs.len() {
        1 => Some(note_path_pairs[0].clone()),
        n => {
            println!("extract_unique_note: bundle has {n} notes addressed to the deposit address");
            None
        },
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_create_transaction(
    inputs: *const *const Transaction,
    payouts: *const u64,
    payout_addresses: *const *const Address,
    advices: *const *const Advice,
    merkle_paths: *const u8,
    merkle_paths_length: usize,
    memo: *const [u8; 512],
) -> *mut PartialTransaction {
    // Convert raw pointers into iterators over references
    let inputs = ref_from_ptr_for_each_party(inputs);
    let payouts = for_each_party(payouts);
    let payout_addresses = ref_from_ptr_for_each_party(payout_addresses);
    let advices = ref_from_ptr_for_each_party(advices);
    // Parse Merkle paths
    let (anchor, merkle_paths) = match parse_merkle_paths(merkle_paths, merkle_paths_length) {
        Some((anchor, paths)) => (anchor, paths),
        _ => return ptr::null_mut(),
    };
    println!("zcash_create_transaction: parsed {} Merkle path(s)", merkle_paths.len());

    let mut notes = Vec::with_capacity(number_of_players() + 1);
    let mut merkle_paths = merkle_paths.into_iter();
    // Pair up Merkle paths with input bundles and find notes of interest
    for (input, advice) in inputs.zip(advices) {
        // Take as many paths as needed for this bundle
        let actions = input.actions();
        let paths = merkle_paths
            .by_ref()
            .take(actions.len());

        // With paths attached to notes/outputs, find the actual note/output of interest, i.e., the one
        // that credits the deposit address and thus corresponds to the advice
        let (note, path) = match extract_unique_note(actions.into_iter(), paths, advice) {
            Some((note, path)) => (note, path),
            _ => return ptr::null_mut(),
        };

        notes.push((note, path, advice));
        println!("zcash_create_transaction: successfully extracted {} input note(s)", notes.len());
    }

    let mut builder = match Builder::new(
        BundleType::DEFAULT,
        BundleVersion::ironwood_v3(),
        BundleVersion::ironwood_v3().default_flags(),
        anchor,
    ) {
        Ok(builder) => builder,
        Err(err) => {
            println!("zcash_create_transaction: failed to create the builder with error {}", err);
            return ptr::null_mut();
        },
    };
    println!("zcash_create_transaction: adding spends...");
    // Add spends
    let result: Result<Vec<_>, _> = notes
        .into_iter()
        .map(|(note, merkle_path, advice)| {
            builder.add_spend_with_alpha(
                advice.fvk.clone(),
                note,
                merkle_path,
                Some(advice.alpha),
            )
        })
        .collect();
    if let Err(err) = result {
        println!("zcash_create_transaction: failed to add spend with error {}", err);
        return ptr::null_mut();
    }

    println!("zcash_create_transaction: adding outputs...");
    // Add outputs
    payouts
        .zip(payout_addresses)
        .for_each(|(payout, recipient)| {
            // We do not check for payout > 0 as we would be adding dummy outputs anyway
            // to not leak any information about contract result; we also use dummy outgoing
            // viewing key
            builder.add_output(
                None,
                recipient.clone(),
                NoteValue::from_raw(*payout),
                unsafe { *memo },
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
    let pk = ProvingKey::build(OrchardCircuitVersion::PostNu6_3);
    let mut rng = SgxRng;
    // Compute the signature digest of a transaction that has only this Orchard bundle
    let sighash = signature_digest(&unauthorized);
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
            let domain = IronwoodDomain::for_action(action);
            try_note_decryption(&domain, &ivk, action)
                .map(|(note, _recipient, _memo)| note.value().inner())
        })
        .sum()
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_hash_transaction(sighash: *mut [u8; 32], txid: *mut [u8; 32], transaction: *const PartialTransaction) {
    unsafe { *sighash = signature_digest(transaction.as_ref().unwrap()); }
    unsafe { *txid = txid_digest(transaction.as_ref().unwrap()); }
}

type Signature = redpallas::Signature<redpallas::SpendAuth>;

#[unsafe(no_mangle)]
pub extern "C" fn zcash_sign_transaction(key: *const SpendingKey, advice: *const Advice, sighash: *const [u8; 32]) -> *mut Signature {
    let ask: SpendAuthorizingKey = unsafe { key.as_ref() }.unwrap().into();
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
    drop(unsafe { Box::from_raw(signature) });
}
