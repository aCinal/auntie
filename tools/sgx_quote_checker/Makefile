
.PHONY: all, clean

CC = gcc
LD = gcc

QVL_PATH = $(SGX_DCAP)/QuoteVerification/dcap_quoteverify
QVE_PATH = $(SGX_DCAP)/QuoteVerification/QvE

EXE = sgx_quote_checker

all: $(EXE)

CFLAGS = \
	-c \
	-O2 \
	-I$(SGX_SDK)/include \
	-I$(QVL_PATH)/inc \
	-I$(QVE_PATH)/Include

LDFLAGS = \
	-L$(QVL_PATH)/linux \
	-lsgx_dcap_quoteverify \
	-lcrypto

SRCS = \
	main.c

OBJS = $(SRCS:.c=.o)

%.o: %.c Makefile
	$(CC) $< -o $@ $(CFLAGS)

$(EXE): $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(EXE) *.o
