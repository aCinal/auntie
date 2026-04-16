include untrusted.mak
include trusted.mak

.PHONY: operator player clean

operator:
	$(MAKE) -C operator

player:
	$(MAKE) -C player

clean:
	$(MAKE) -C operator clean
	$(MAKE) -C player clean
