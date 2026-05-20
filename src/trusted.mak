
export ENC_COMMON_DIR := $(COMMON_PREFIX)/trusted

# TODO: This should be cleaned up, so that the common files get built separately for operator and player
export ENC_COMMON_C_FILES := \
	$(ENC_COMMON_DIR)/channel.c \
	$(ENC_COMMON_DIR)/messages.c \
	$(ENC_COMMON_DIR)/mutual_attestation_common.c \
	$(ENC_COMMON_DIR)/printf.c \
	$(ENC_COMMON_DIR)/zcash.c

export ENC_COMMON_INC_DIR_FLAGS := \
	-I$(SGX_SDK)/include \
	-I$(SGX_SDK)/include/tlibc \
	-I$(ENC_COMMON_DIR)

export ENC_COMMON_C_FLAGS := \
	$(SGX_COMMON_FLAGS) \
	-nostdinc \
	-fvisibility=hidden \
	-fstack-protector \
	$(ENC_COMMON_INC_DIR_FLAGS) \
	-fno-builtin-printf \
	-DAUNTIE_NUM_PLAYERS=$(AUNTIE_NUM_PLAYERS) \
	-DAUNTIE_OPERATOR_COLLATERAL=$(AUNTIE_OPERATOR_COLLATERAL) \
	-DAUNTIE_SETTLE_DELAY_BLOCKS=$(AUNTIE_SETTLE_DELAY_BLOCKS) \
	-DAUNTIE_REFUND_DELAY_BLOCKS=$(AUNTIE_REFUND_DELAY_BLOCKS) \
	-DAUNTIE_MINER_FEE_PER_PLAYER=$(AUNTIE_MINER_FEE_PER_PLAYER) \
	-DAUNTIE_OPERATOR_FEE_PER_PLAYER=$(AUNTIE_OPERATOR_FEE_PER_PLAYER)

ifeq ($(SGX_DEBUG), 1)
	ENC_COMMON_C_FLAGS     += -DDEBUG -UNDEBUG -UEDEBUG
else
	ENC_COMMON_C_FLAGS     += -DNDEBUG -UEDEBUG -UDEBUG
endif

export ENC_COMMON_LD_FLAGS := \
	$(SGX_COMMON_FLAGS) \
	-Wl,--no-undefined \
	-nostdlib \
	-nodefaultlibs \
	-nostartfiles \
	-L$(SGX_LIBRARY_PATH) \
	-Wl,--whole-archive -lsgx_trts -Wl,--no-whole-archive \
	-Wl,--start-group \
	-lsgx_tstdc \
	-lsgx_tcxx \
	-lsgx_pthread \
	-lsgx_tcrypto \
	-lsgx_tservice \
	-Wl,--end-group \
	-Wl,-Bstatic \
	-Wl,-Bsymbolic \
	-Wl,-pie \
	-Wl,-eenclave_entry \
	-Wl,--export-dynamic \
	-Wl,--defsym,__ImageBase=0 \
	-Wl,-z,relro,-z,now,-z,noexecstack

export RUST_PACKAGES = zcash_ffi
export RUST_DIR = $(ENC_COMMON_DIR)/rust
export RUST_LIB_NAMES = $(addsuffix .a,$(addprefix lib,$(RUST_PACKAGES)))
export RUST_LIB_PATHS = $(addprefix $(RUST_DIR)/,$(RUST_LIB_NAMES))
