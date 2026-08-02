
export SGX_DEBUG                      ?= 0

# Set to the install prefix of linux-sdk (https://github.com/intel/linux-sgx)
export SGX_SDK                        ?= /opt/intel/sgxsdk
export SGX_DCAP                       ?= /opt/intel/sgx-dcap

# Paths where libraries (custom enclave-friendly libc, etc.) can be found, as well as tools for signing or generating glue code
export SGX_LIBRARY_PATH               := $(SGX_SDK)/lib64
export SGX_ENCLAVE_SIGNER             := $(SGX_SDK)/bin/x64/sgx_sign
export SGX_EDGER8R                    := $(SGX_SDK)/bin/x64/sgx_edger8r

# We build the untrusted app and the trusted enclave separately. Specify flags common to both.
export SGX_COMMON_FLAGS               := -m64

ifeq ($(SGX_DEBUG), 1)
	SGX_COMMON_FLAGS                  += -O0 -g -Werror -Wall
else
	SGX_COMMON_FLAGS                  += -O2 -Werror -Wall
endif

export ROOT_DIR                       := $(shell pwd)
export COMMON_PREFIX                  := $(ROOT_DIR)/src/common

export OPERATOR_APP_EXE               := auntie_operator
export OPERATOR_ENCLAVE_SO            := auntie_operator_tee.signed.so
export OPERATOR_ENCLAVE_NAME          := auntie_operator_tee

export PLAYER_APP_EXE                 := auntie_player
export PLAYER_ENCLAVE_SO              := auntie_player_tee.signed.so
export PLAYER_ENCLAVE_NAME            := auntie_player_tee

# n in the paper
export AUNTIE_NUM_PLAYERS             := 3
# delta in the paper
export AUNTIE_OPERATOR_COLLATERAL     := 10000
# tau in the paper
export AUNTIE_REFUND_DELAY_BLOCKS     := 40
# tau' in the paper
export AUNTIE_SETTLE_DELAY_BLOCKS     := 80

# Checkpoint block hash in RPC byte order
export AUNTIE_CHECKPOINT_BLOCK_HASH   := 000136c7dd12037fe5f170360c745cddb20080afa17e8b0720d59e291e6e144b

# Use marginal fee of 5000 zatoshis per action as recommended in https://zips.z.cash/zip-0317
export AUNTIE_MINER_FEE_PER_PARTY     := 5000
export AUNTIE_OPERATOR_FEE_PER_PLAYER := 5000

# Set to 1 to build for testnet
export AUNTIE_TESTNET                 := 0

export SIGNING_KEY                    := $(ROOT_DIR)/resources/mrsigner.priv

export EDGER8R_COMMON_SEARCH_PATHS := \
	--search-path $(SGX_SDK)/include \
	--search-path $(COMMON_PREFIX)

.PHONY: all operator player tools clean

all: operator player tools

operator:
	$(MAKE) -C src operator
	@cp src/operator/trusted/$(OPERATOR_ENCLAVE_SO) .
	@cp src/operator/untrusted/$(OPERATOR_APP_EXE) .

player:
	$(MAKE) -C src player
	@cp src/player/trusted/$(PLAYER_ENCLAVE_SO) .
	@cp src/player/untrusted/$(PLAYER_APP_EXE) .

tools:
	$(MAKE) -C tools/sgx_quote_checker

clean:
	@rm -f $(OPERATOR_ENCLAVE_SO)
	@rm -f $(OPERATOR_APP_EXE)
	@rm -f $(PLAYER_ENCLAVE_SO)
	@rm -f $(PLAYER_APP_EXE)
	$(MAKE) -C src clean
	$(MAKE) -C tools/sgx_quote_checker clean
