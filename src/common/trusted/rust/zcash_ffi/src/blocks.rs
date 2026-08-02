use alloc::{
    vec,
    vec::Vec,
    slice,
};
use corez::io::Read;
use orchard::keys::SpendingKey;
use sgx::println;
use sha2::{Digest, Sha256};
use uint::construct_uint;

construct_uint! {
    pub struct U256(4);
}

const BLOCK_HEADER_LENGTH: usize = 1487;

fn checkpoint_block_hash() -> [u8; 32] {
    let mut block_hash = [0u8; 32];
    hex::decode_to_slice(env!("AUNTIE_CHECKPOINT_BLOCK_HASH"), &mut block_hash).unwrap();
    // Expect AUNTIE_CHECKPOINT_BLOCK_HASH to be in RPC encoding, but need wire encoding
    block_hash.reverse();
    block_hash
}

fn sha256d(inputs: &[&[u8]]) -> [u8; 32] {
    let mut inner = Sha256::new();
    inputs
        .iter()
        .for_each(|chunk| inner.update(chunk));
    Sha256::digest(inner.finalize()).into()
}

fn hash_prev_block(block: &[u8; BLOCK_HEADER_LENGTH]) -> [u8; 32] {
    block[4..36].try_into().unwrap()
}

fn hash_merkle_root(block: &[u8; BLOCK_HEADER_LENGTH]) -> [u8; 32] {
    block[36..68].try_into().unwrap()
}

fn hash_this_block(block: &[u8; BLOCK_HEADER_LENGTH]) -> [u8; 32] {
    sha256d(&[block])
}

fn correct_equihash_solution(block: &[u8; BLOCK_HEADER_LENGTH]) -> bool {
    // Check the Equihash solution
    let input = &block[..108];
    let nonce = &block[108..140];
    let solution = &block[143..];
    // The Equihash parameters are n = 200, k = 9 as per https://zips.z.cash/protocol/protocol.pdf#equihash
    equihash::is_valid_solution(200, 9, input, nonce, solution).is_ok()
}

fn passes_difficulty_filter(block: &[u8; BLOCK_HEADER_LENGTH]) -> bool {
    // Check the difficulty filter (see https://zips.z.cash/protocol/protocol.pdf#difficulty). Note that we cannot tell
    // if difficulty adjustment was done correctly, as the host trying to forge a subchain can provide any timestamps
    // (nTime) they want. We assume the worst and check that SHA-256d(block) satisfies the most permissive difficulty filter.
    // Specifically, https://zips.z.cash/protocol/protocol.pdf#diffadjustment says that, for honestly mined blocks, we have
    // by consensus that
    //
    //            nBits = ThresholdBits(height) = ToCompact(Threshold(height)), where Threshold(height) <= PoWLimit
    //
    // where nBits is the "compact" difficulty declared in the block header and PoWLimit = 2^243 - 1 for Mainnet
    // (see https://zips.z.cash/protocol/protocol.pdf#constants) and 2^251 - 1 for Testnet. The difficulty filter dictates
    // we check that
    //
    //                                             SHA-256d(block) <= ToTarget(nBits)
    //
    // when viewing the block digest as a little-endian 256-bit integer, but ToTarget(ToCompact(x)) <= x (it only rounds down),
    // and so we can check that
    //
    //     SHA-256d(block) <= ToTarget(nBits) = ToTarget(ToCompact(Threshold(height))) <= Threshold(height) <= PoWLimit.
    //
    let digest = hash_this_block(block);
    let actual = U256::from_little_endian(&digest);

    #[cfg(auntie_testnet)]
    let target = U256::from(1) << 251;
    #[cfg(not(auntie_testnet))]
    let target = U256::from(1) << 243;

    actual < target
}

fn verify_block_header(block: &[u8; BLOCK_HEADER_LENGTH], previous_block_hash: [u8; 32]) -> Option<[u8; 32]> {
    if correct_equihash_solution(block) && passes_difficulty_filter(block) && hash_prev_block(block) == previous_block_hash {
        Some(hash_this_block(block))
    } else {
        None
    }
}

fn transactions_merkle_root(txids: Vec<[u8; 32]>) -> [u8; 32] {
    // Keep track of hashes at each layer of the tree
    let mut hashes: Vec<_> = txids.into_iter().collect();
    while hashes.len() > 1 {
        // Pair them up and pad any odd ones via repetition
        hashes = hashes
            .chunks(2)
            .map(|chunk| match chunk {
                [left, right] => sha256d(&[left, right]),
                [left] => sha256d(&[left, left]),
                _ => unreachable!("chunks(2)"),
            })
            .collect();
    }
    hashes[0]
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_authorized_and_buried(txid: *const [u8; 32], _deposit_key: *const SpendingKey, blocks: *const u8, blocks_length: usize) -> i32 {
    // For delay of settlement, we need both the block headers (to verify the delay) and also the IDs of the transactions in the blocks to verify that
    // the settlement transaction was really mined. Expect the following format: [header1][count1 as u32][txs1]...
    let blocks = unsafe { slice::from_raw_parts(blocks, blocks_length) };
    let txid = unsafe { txid.as_ref() }.unwrap();

    let mut previous_block_hash = checkpoint_block_hash();
    let mut count_since_settlement = 0;
    let mut settlement_found = false;
    let mut stream = blocks;
    loop {
        // Read in the block header
        let mut block = [0u8; BLOCK_HEADER_LENGTH];
        if stream.read_exact(&mut block).is_err() {
            break count_since_settlement;
        }

        previous_block_hash = match verify_block_header(&block, previous_block_hash) {
            Some(digest) => digest,
            _ => {
                println!("zcash_authorized_and_buried: invalid block header");
                break count_since_settlement;
            },
        };

        // Read in the number of transactions in the current block
        let mut transactions_in_block = [0u8; 4];
        if stream.read_exact(&mut transactions_in_block).is_err() {
            break count_since_settlement;
        }
        let transactions_in_block = u32::from_le_bytes(transactions_in_block) as usize;

        // Read in the transactions
        let mut transactions = vec![0u8; transactions_in_block * 32];
        if stream.read_exact(&mut transactions).is_err() {
            break count_since_settlement;
        }
        let transactions: Vec<_> = transactions
            .chunks_exact(32)
            .map(|chunk| chunk.try_into().unwrap())
            .collect();

        // TODO: Only check tree consistency for the block with settlement?

        // If we are past settlement, increment the count (the Merkle root may even be inconsistent at this point)
        if settlement_found {
            count_since_settlement = count_since_settlement + 1;
        }

        // See if the settlement transaction is in the block
        if transactions.contains(txid) {
            settlement_found = true;
        }

        // Compute a Merkle root over the IDs to ensure the transactions are consistent with the block header
        if transactions_merkle_root(transactions) != hash_merkle_root(&block) {
            println!("zcash_authorized_and_buried: transactions inconsistent with block header");
            break count_since_settlement;
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_blocks_since_checkpoint(blocks: *const u8, blocks_length: usize) -> i32 {
    // For contract timeout, we do not care about the contents of the blocks (transactions in them),
    // and we expect the player/operator to only supply a list of block _headers_ (which have fixed length)

    // With chunks_exact we quietly ignore any remainder bytes not fitting into a block
    let blocks = unsafe { slice::from_raw_parts(blocks, blocks_length) }
        .chunks_exact(BLOCK_HEADER_LENGTH)
        .map(|chunk| chunk.try_into().unwrap());

    // For each block header, check that it correctly links to the previous one and has correct proof of work
    let mut previous_block_hash = checkpoint_block_hash();
    let mut count = 0;
    for block in blocks {
        previous_block_hash = match verify_block_header(&block, previous_block_hash) {
            Some(digest) => digest,
            _ => {
                println!("zcash_blocks_since_checkpoint: invalid block header");
                return count;
            },
        };
        count = count + 1;
    }

    count
}
