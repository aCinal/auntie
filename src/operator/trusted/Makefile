ENC_CONFIG_FILE     := config.xml

# Use the .so name prescribed by the top-level Makefile
SIGNED_SO_NAME      := $(OPERATOR_ENCLAVE_SO)
SO_NAME             := $(SIGNED_SO_NAME:.signed.so=.so)

ENC_C_FILES := \
	$(ENC_COMMON_C_FILES) \
	core.c \
	entry.c \
	functionality.c \
	memo.c \
	mutual_attestation.c \
	players.c

ENC_INC_DIR_FLAGS := \
	-I.

ENC_C_FLAGS := \
	$(ENC_COMMON_C_FLAGS) \
	$(ENC_INC_DIR_FLAGS)

ENC_LD_FLAGS := \
	$(ENC_COMMON_LD_FLAGS)

# edger8r derives the names for the glue files from the EDL file
TRUSTED_GLUE_SOURCE := $(EDL_NAME:.edl=_t.c)
TRUSTED_GLUE_OBJECT := $(EDL_NAME:.edl=_t.o)

ENC_O_FILES         := $(ENC_C_FILES:.c=.o)
ENC_O_FILES         += $(TRUSTED_GLUE_OBJECT)

.PHONY: all clean rust

all: $(SIGNED_SO_NAME)

rust:
	$(MAKE) -C $(RUST_DIR)

# Generate the glue code with edger8r
$(TRUSTED_GLUE_SOURCE): $(SGX_EDGER8R) $(EDL_PATH) Makefile
	@echo "Generating trusted glue code at $@"
	@$(SGX_EDGER8R) --trusted $(EDL_PATH) $(EDGER8R_SEARCH_PATHS)

# Compile regular source files
%.o: %.c Makefile $(TRUSTED_GLUE_SOURCE)
	@echo "Compiling source $<"
	@$(CC) $(ENC_C_FLAGS) -c $< -o $@

# Link everything together
$(SO_NAME): $(ENC_O_FILES) rust
	@echo "Linking the enclave from: $(ENC_O_FILES) $(RUST_LIB_PATHS)"
	@$(CC) $(ENC_O_FILES) $(RUST_LIB_PATHS) -o $@ $(ENC_LD_FLAGS)

# Sign the enclave
$(SIGNED_SO_NAME): $(SO_NAME) $(ENC_CONFIG_FILE)
	@echo "Signing the enclave with a key at $(SIGNING_KEY)"
	@$(SGX_ENCLAVE_SIGNER) sign -key $(SIGNING_KEY) -enclave $(SO_NAME) -out $(SIGNED_SO_NAME) -config $(ENC_CONFIG_FILE)

clean:
	@rm -f $(SIGNED_SO_NAME) $(SO_NAME) $(ENC_O_FILES)
	@rm -f $(EDL_NAME:.edl=_t.)*
	$(MAKE) -C $(RUST_DIR) clean
