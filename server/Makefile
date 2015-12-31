ifndef CONTIKI
  ${error CONTIKI not defined! You must specify where Contiki resides}
endif

ifndef CC65_HOME
  ${error CC65_HOME not defined! You must specify where cc65 resides}
endif

ifndef C1541
  ${error C1541 not defined! You must specify where VICE c1541 resides}
endif

all: c64 c128

define makes
$1-makes:
	$(MAKE) -C $(CONTIKI)/cpu/6502/ethconfig        TARGET=$1
	$(MAKE) -C $(CONTIKI)/cpu/6502/ipconfig         TARGET=$1
	$(MAKE) -f Makefile.vchar64d                    TARGET=$1
endef

$(eval $(call makes,c64))

c64: vchar64d-c64.d64

vchar64d-c64.d64: c64-makes
	$(C1541) -format vchar64-server,00 d64 $@
	$(C1541) -attach $@ -write vchar64d.c64 				        vchar64-server,p
	$(C1541) -attach $@ -write $(CONTIKI)/cpu/6502/ethconfig/ethconfig.c64     	ethconfig,p
	$(C1541) -attach $@ -write $(CONTIKI)/cpu/6502/ipconfig/ipconfig.c64         	ipconfig,p
	$(C1541) -attach $@ -write contiki.cfg 	                            		contiki.cfg,s

$(eval $(call makes,c128))

c128: vchar64d-c128.d71

vchar64d-c128.d71: c128-makes
	$(C1541) -format contiki-1,00 d71 $@
	$(C1541) -attach $@ -write vchar64d.c128 				        vchar64-server,p
	$(C1541) -attach $@ -write $(CONTIKI)/cpu/6502/ethconfig/ethconfig.c128     	ethconfig,p
	$(C1541) -attach $@ -write $(CONTIKI)/cpu/6502/ipconfig/ipconfig.c128         	ipconfig,p
	$(C1541) -attach $@ -write contiki.cfg 	                            		contiki.cfg,s

