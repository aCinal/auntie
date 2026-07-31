use blake2b_simd::Hash as Blake2bHash;
use orchard::bundle::{Authorization, Bundle};
use zcash_primitives::transaction::{
    sighash::{signature_hash, SignableInput},
    txid::{to_txid, TxIdDigester},
    Authorization as TxAuthorization, TransactionData, TxDigests, TxVersion,
};
use zcash_protocol::{
    consensus::{BlockHeight, BranchId},
    value::ZatBalance,
};

// Compute ZIP-244 digests (https://zips.z.cash/zip-0244)

const CONSENSUS_BRANCH_ID: BranchId = BranchId::Nu6_3;

struct DigestAuth<A>(core::marker::PhantomData<A>);
// To call TransactionData::from_parts_v6 we need to define impl TxAuthorization
// that agrees with the authorization stage of our Orchard/Ironwood bundle, which
// will either be InProgress<Proof, Unauthorized> (when called from zcash_create_transaction)
// or InProgress<Proof, PartiallyAuthorized> (when called from zcash_hash_transaction);
// use generics to handle both cases cleanly
impl<A: Authorization> TxAuthorization for DigestAuth<A> {
    type TransparentAuth = zcash_transparent::builder::Coinbase;
    type SaplingAuth = sapling_crypto::bundle::Authorized;
    type OrchardAuth = A;
}

fn tx_data_and_digests<A: Authorization + Clone>(
    bundle: &Bundle<A, ZatBalance>,
) -> (TransactionData<DigestAuth<A>>, TxDigests<Blake2bHash>)
where
    A::SpendAuth: Clone,
{
    // TODO: Study if setting nExpiryHeight (https://zips.z.cash/zip-0203) to, say,
    //       the checkpoint block height + AUNTIE_*_DELAY_BLOCKS can do us some good
    let tx_data = TransactionData::<DigestAuth<A>>::from_parts_v6(
        CONSENSUS_BRANCH_ID,
        0,                         // lock_time
        BlockHeight::from_u32(0),  // expiry_height
        None,                      // transparent_bundle
        None,                      // sapling_bundle
        None,                      // orchard_bundle
        Some(bundle.clone()),      // ironwood_bundle
    );
    let digests = tx_data.digest(TxIdDigester);
    (tx_data, digests)
}

pub fn signature_digest<A: Authorization + Clone>(bundle: &Bundle<A, ZatBalance>) -> [u8; 32]
where
    A::SpendAuth: Clone,
{
    let (tx_data, txid_parts) = tx_data_and_digests(bundle);
    *signature_hash(&tx_data, &SignableInput::Shielded, &txid_parts).as_ref()
}

pub fn txid_digest<A: Authorization + Clone>(bundle: &Bundle<A, ZatBalance>) -> [u8; 32]
where
    A::SpendAuth: Clone,
{
    let (_tx_data, txid_parts) = tx_data_and_digests(bundle);
    *to_txid(TxVersion::V6, CONSENSUS_BRANCH_ID, &txid_parts).as_ref()
}
