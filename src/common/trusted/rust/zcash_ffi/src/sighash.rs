use orchard::bundle::{Authorization, Bundle};

const HEADER: u32 = 0x8000_0005;               // Version 5 with the fOverwintered flag (bit 31) set
const VERSION_GROUP_ID: u32 = 0x26A7_270A;     // See "7.1.2 Transaction Consensus Rules" in https://zips.z.cash/protocol/protocol.pdf
const CONSENSUS_BRANCH_ID: u32 = 0xC2D6_D0B4;  // NU5 branch ID

pub fn signature_hash<A: Authorization, V: Copy + Into<i64>>(bundle: &Bundle<A, V>) -> [u8; 32] {
    // Compute a ZIP-244 signature hash for a transaction with just the Orchard bundle provided,
    // no transparent bundle, and no Sapling bundle (see https://zips.z.cash/zip-0244)

    let hasher = |personal: &[u8; 16]| {
        blake2b_simd::Params::new()
            .hash_length(32)
            .personal(personal)
            .to_state()
    };

    // According to ZIP-244, the signature hash is computed as a BLAKE2b-256 hash of the following values:
    //   S.1: header_digest          (32-byte hash output)
    //   S.2: transparent_sig_digest (32-byte hash output)
    //   S.3: sapling_digest         (32-byte hash output)
    //   S.4: orchard_digest         (32-byte hash output)
    // with personalization field set to "ZcashTxHash_" || CONSENSUS_BRANCH_ID

    // S.1: header_digest
    let header_digest = {
        let mut h = hasher(b"ZTxIdHeadersHash");
        h.update(&HEADER.to_le_bytes());
        h.update(&VERSION_GROUP_ID.to_le_bytes());
        h.update(&CONSENSUS_BRANCH_ID.to_le_bytes());
        h.update(&0u32.to_le_bytes());  // lock_time
        h.update(&0u32.to_le_bytes());  // nExpiryHeight
        h.finalize()
    };

    // S.2: transparent_sig_digest
    let transparent_digest = hasher(b"ZTxIdTranspaHash").finalize();
    // S.3: sapling_digest
    let sapling_digest = hasher(b"ZTxIdSaplingHash").finalize();
    // S.4: orchard_digest
    let orchard_digest = bundle.commitment().0;

    // The final signature hash
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
