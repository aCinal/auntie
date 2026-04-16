
.PHONY: all clean

export EDL_NAME := auntie_operator.edl
export EDL_PATH := $(shell pwd)/$(EDL_NAME)
export EDGER8R_SEARCH_PATHS := $(EDGER8R_COMMON_SEARCH_PATHS)

all:
	$(MAKE) -C trusted
	$(MAKE) -C untrusted

clean:
	$(MAKE) -C trusted clean
	$(MAKE) -C untrusted clean
