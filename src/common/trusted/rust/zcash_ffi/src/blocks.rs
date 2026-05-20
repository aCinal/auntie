use crate::sighash::signature_hash;
use orchard::keys::SpendingKey;
use sgx::println;

// TODO: define checkpoint_block here

// TODO: Use actual chain of blocks here
use crate::transactions::{Transaction, zcash_import_transaction, zcash_release_transaction};
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
    println!("zcash_authorized_and_buried stub called: checking only that the settlement transaction is valid as delay of settlement is not yet implemented");

    let actual = signature_hash(unsafe { blocks.as_ref() }.unwrap());
    if unsafe { *sighash } == actual {
        env!("AUNTIE_SETTLE_DELAY_BLOCKS").parse().unwrap()
    } else {
        0
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn zcash_blocks_since_checkpoint(_blocks: *const Blocks) -> i32 {
    println!("zcash_blocks_since_checkpoint returning 0, as contract timeout is not yet implemented");
    0
}
