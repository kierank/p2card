#
# Makefile of P2 Card driver
#
## $Id: Makefile 21639 2013-04-09 00:15:24Z Yasuyuki Matsumoto $

#TARGET_KERNEL   = /lib/modules/$(shell uname -r)/build
#TARGET_ROOT	= /

# --------------------------------------------------------------------------
# [[ for P2V3 ]]
TOPDIR	= ../../..
-include $(TOPDIR)/mk/p2pf.config.mk
-include $(TOPDIR)/mk/p2pf.sys.mk

ifdef P2PF_KERNEL_PATH
  TARGET_KERNEL = $(P2PF_KERNEL_PATH)
else
  ifeq ($(wildcard $(TOPDIR)/src/contrib/linux-2.6.27-p2),$(TOPDIR)/src/contrib/linux-2.6.27-p2)
    TARGET_KERNEL = $(TOPDIR)/src/contrib/linux-2.6.27-p2
  else
    ifeq ($(wildcard $(TOPDIR)/src/contrib/linux-2.6.25-p2),$(TOPDIR)/src/contrib/linux-2.6.25-p2)
      TARGET_KERNEL = $(TOPDIR)/src/contrib/linux-2.6.25-p2
    endif
  endif
endif
TARGET_ROOT     = $(TOPDIR)/src/fs/driver_$(P2PF_TARGET)_$(P2PF_HW_VERSION)
# --------------------------------------------------------------------------

#
# Configuration options
#
DEBUG	= no

#EXTRA_CFLAGS += -DSPD_USE_MMIO=1
EXTRA_CFLAGS += -DSPD_CONFIG_RAW=1
EXTRA_CFLAGS += -DSPD_CONFIG_USB=1
#EXTRA_CFLAGS += -DSPD_NOCHECK_PAGENUM=1
#EXTRA_CFLAGS += -DDBG_LEVEL=DL_DEBUG
#EXTRA_CFLAGS += -DDBG_LEVEL=DL_INFO
#EXTRA_CFLAGS += -DDBG_LEVEL=DL_NOTICE
EXTRA_CFLAGS += -DDBG_LEVEL=DL_ERROR
#EXTRA_CFLAGS += -DDBG_TRACE=1
#EXTRA_CFLAGS += -DDBG_ASSERT=1
#EXTRA_CFLAGS += -DDBG_COMMAND_PRINT=1
#EXTRA_CFLAGS += -DDBG_DELAYPROC=1
#EXTRA_CFLAGS += -DDBG_PRINT_SG=1
#EXTRA_CFLAGS += -DDBG_PRINT_BVEC=1
#EXTRA_CFLAGS += -DDBG_HOTPLUG_TEST=1
#EXTRA_CFLAGS += -DDBG_SET_PARAMS=1
#EXTRA_CFLAGS += -DDBG_DUMP_DATA=1
#EXTRA_CFLAGS += -DSPD_FAST_CHECK_LK=1
#EXTRA_CFLAGS += -DSPD_WAIT_MINISD


# --------------------------------------------------------------------------
# [[ for P2V3 ]]
ARCH_FLAG = CONFIG_P2PF_$(P2PF_TARGET)

ifeq ($(P2PF_TARGET),K000)
ARCH_FLAG = CONFIG_P2PF_SAV8313_K200
#ARCH_FLAG = CONFIG_P2PF_SAV8313_K230
endif
ifeq ($(P2PF_TARGET),K001)
ARCH_FLAG = CONFIG_P2PF_MPC83XXBRB
endif
ifeq ($(P2PF_TARGET),DRV01)
ARCH_FLAG = CONFIG_P2PF_E605
endif

EXTRA_CFLAGS += -D$(ARCH_FLAG)
# --------------------------------------------------------------------------

spd_mod-objs := spd.o spd_hwif.o spd_bdev.o spd_rdev.o spd_udev.o spd_drct.o spd_adpt.o
obj-m +=  spd_mod.o

all:
	make -C $(TARGET_KERNEL) M=$(PWD) modules

clean:
	make -C $(TARGET_KERNEL) M=$(PWD) clean
	rm -rf *~ *.o *.ko .*.cmd .*.d .*.tmp *.mod.c *.symtypes modules.order */*~

distclean: clean

install: all
	make -C  $(TARGET_KERNEL) M=$(PWD) INSTALL_MOD_PATH=$(TARGET_ROOT) modules_install

setup:

teardown:

