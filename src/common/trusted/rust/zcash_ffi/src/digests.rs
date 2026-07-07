use orchard::bundle::{Authorization, Bundle};

const HEADER: u32 = 0x8000_0005;               // Version 5 with the fOverwintered flag (bit 31) set
const VERSION_GROUP_ID: u32 = 0x26A7_270A;     // See https://zips.z.cash/protocol/protocol.pdf#txnconsensus
const CONSENSUS_BRANCH_ID: u32 = 0x5437_F330;  // NU6.2 branch ID

pub fn signature_digest<A: Authorization, V: Copy + Into<i64>>(bundle: &Bundle<A, V>) -> [u8; 32] {
    // Compute a ZIP-244 signature digest for a transaction with just the Orchard bundle provided,
    // no transparent bundle, and no Sapling bundle, which, according to https://zips.z.cash/zip-0244,
    // is identical to the txid digest
    txid_digest(bundle)
}

pub fn txid_digest<A: Authorization, V: Copy + Into<i64>>(bundle: &Bundle<A, V>) -> [u8; 32] {
    // Compute a ZIP-244 txid digest for a transaction with just the Orchard bundle provided,
    // no transparent bundle, and no Sapling bundle (see https://zips.z.cash/zip-0244)

    let hasher = |personal: &[u8; 16]| {
        blake2b_simd::Params::new()
            .hash_length(32)
            .personal(personal)
            .to_state()
    };

    // According to ZIP-244, the txid hash is computed as a BLAKE2b-256 hash of the following values:
    //   T.1: header_digest          (32-byte hash output)
    //   T.2: transparent_sig_digest (32-byte hash output)
    //   T.3: sapling_digest         (32-byte hash output)
    //   T.4: orchard_digest         (32-byte hash output)
    // with personalization field set to "ZcashTxHash_" || CONSENSUS_BRANCH_ID

    // T.1: header_digest
    let header_digest = {
        let mut h = hasher(b"ZTxIdHeadersHash");
        h.update(&HEADER.to_le_bytes());
        h.update(&VERSION_GROUP_ID.to_le_bytes());
        h.update(&CONSENSUS_BRANCH_ID.to_le_bytes());
        h.update(&0u32.to_le_bytes());  // lock_time
        h.update(&0u32.to_le_bytes());  // nExpiryHeight
        h.finalize()
    };

    // T.2: transparent_digest
    let transparent_digest = hasher(b"ZTxIdTranspaHash").finalize();
    // T.3: sapling_digest
    let sapling_digest = hasher(b"ZTxIdSaplingHash").finalize();
    // T.4: orchard_digest
    let orchard_digest = bundle.commitment().0;

    // The final txid hash
    let mut personal = [0u8; 16];
    personal[..12].copy_from_slice(b"ZcashTxHash_");
    personal[12..].copy_from_slice(&CONSENSUS_BRANCH_ID.to_le_bytes());
    let mut h = hasher(&personal);
    h.update(header_digest.as_bytes());
    h.update(transparent_digest.as_bytes());
    h.update(sapling_digest.as_bytes());
    h.update(orchard_digest.as_bytes());

    h.finalize().as_bytes().try_into().unwrap()
}
