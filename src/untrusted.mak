
export APP_COMMON_DIR := $(COMMON_PREFIX)/untrusted

# TODO: This should be cleaned up, so that the common files get built separately for operator and player
export APP_COMMON_C_FILES := \
	$(APP_COMMON_DIR)/common_ocalls.c \
	$(APP_COMMON_DIR)/enclave.c \
	$(APP_COMMON_DIR)/shell.c \
	$(APP_COMMON_DIR)/utils.c

export APP_COMMON_INC_DIR_FLAGS := \
	-I$(SGX_SDK)/include \
	-I$(APP_COMMON_DIR) \
	-I$(SGX_DCAP)/QuoteGeneration/quote_wrapper/ql/inc

export APP_COMMON_C_FLAGS := \
	$(APP_COMMON_INC_DIR_FLAGS) \
	$(SGX_COMMON_FLAGS) \
	-Wno-attributes \
	-DAUNTIE_NUM_PLAYERS=$(AUNTIE_NUM_PLAYERS)

ifeq ($(SGX_DEBUG), 1)
	APP_COMMON_C_FLAGS       += -DDEBUG -UNDEBUG -UEDEBUG
else
	APP_COMMON_C_FLAGS       += -DNDEBUG -UEDEBUG -UDEBUG
endif

export APP_COMMON_LD_FLAGS := \
	$(SGX_COMMON_FLAGS) \
	-L$(SGX_LIBRARY_PATH) \
	-lsgx_urts \
	-L$(SGX_DCAP)/QuoteGeneration/build/linux \
	-lsgx_qe3_logic \
	-lsgx_pce_logic \
	-lsgx_dcap_ql \
