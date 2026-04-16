
APP_C_FILES := \
	$(APP_COMMON_C_FILES) \
	main.c \
	ocalls.c

APP_INC_DIR_FLAGS := \
	-I.

APP_C_FLAGS := \
	$(APP_COMMON_C_FLAGS) \
	$(APP_INC_DIR_FLAGS) \
	-DOPERATOR_ENCLAVE_SO=\"$(OPERATOR_ENCLAVE_SO)\"

APP_LD_FLAGS := \
	$(APP_COMMON_LD_FLAGS)

# edger8r derives the names for the glue files from the EDL file
UNTRUSTED_GLUE_SOURCE := $(EDL_NAME:.edl=_u.c)
UNTRUSTED_GLUE_OBJECT := $(EDL_NAME:.edl=_u.o)

APP_O_FILES           := $(APP_C_FILES:.c=.o)
APP_O_FILES           += $(UNTRUSTED_GLUE_OBJECT)

.PHONY: all clean

all: $(OPERATOR_APP_EXE)

# Generate glue code
$(UNTRUSTED_GLUE_SOURCE): $(SGX_EDGER8R) $(EDL_PATH) Makefile
	@echo "Generating untrusted glue code at $@"
	@$(SGX_EDGER8R) --untrusted $(EDL_PATH) $(EDGER8R_SEARCH_PATHS)

# Compile sources
%.o: %.c Makefile $(UNTRUSTED_GLUE_SOURCE)
	@echo "Compiling $<"
	@$(CC) $(APP_C_FLAGS) -c $< -o $@

# Link everything together
$(OPERATOR_APP_EXE): $(APP_O_FILES)
	@echo "Linking the app from: $^"
	@$(CC) $^ -o $@ $(APP_LD_FLAGS)

clean:
	@rm -f $(PLAYER_APP_EXE) $(APP_O_FILES)
	@rm -f $(EDL_NAME:.edl=_u.)*
