
CARGO_FLAGS =
ifeq ($(SGX_DEBUG), 1)
	CARGO_FLAGS +=
	RELEASE = debug
else
	CARGO_FLAGS += --release
	RELEASE = release
endif

.PHONY: all clean $(RUST_PACKAGES)

all: $(RUST_PACKAGES)

# Crate aes by default checks at runtime for availability of AES-NI
# via CPUID which is an illegal instruction inside an SGX enclave,
# see https://docs.rs/aes/latest/aes/
export RUSTFLAGS = -Ctarget-feature=+aes,+ssse3
# The same applies to crates chacha20 (which crate zcash_note_encryption
# uses in version 0.9) and poly1305 (used in version 0.8)
RUSTFLAGS += --cfg chacha20_force_sse2 --cfg poly1305_force_soft
# TODO: change to '--cfg chacha20_backend="sse" --cfg poly1305_backend="soft"'
#       once zcash_note_encryption starts using newer versions of the crates

ifeq ($(AUNTIE_TESTNET), 1)
	RUSTFLAGS += --cfg auntie_testnet
endif

$(RUST_PACKAGES):
	cargo build --manifest-path $(RUST_DIR)/$@/Cargo.toml $(CARGO_FLAGS)
	@cp $(RUST_DIR)/$@/target/$(RELEASE)/lib$@.a $(RUST_DIR)/lib$@.a

%_clean:
	cargo clean --manifest-path $(RUST_DIR)/$*/Cargo.toml
	cargo update --manifest-path $(RUST_DIR)/$*/Cargo.toml
	@rm -f lib$*.a

clean: $(addsuffix _clean, $(RUST_PACKAGES))
