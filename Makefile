
ARCH 		:= aarch64
SUBSYSTEM_APP 	:= 10	# EFI Application
SUBSYSTEM_RT 	:= 12	# EFI Runtime driver
CROSS_COMPILE 	:= aarch64-linux-gnu-

AS             := $(CROSS_COMPILE)as
CC             := $(CROSS_COMPILE)gcc
LD             := $(CROSS_COMPILE)ld
OBJCOPY        := $(CROSS_COMPILE)objcopy

DTC            := dtc

OUT_DIR := $(CURDIR)/out

GNUEFI_DIR = $(CURDIR)/external/gnu-efi
GNUEFI_OUT = $(GNUEFI_DIR)/$(ARCH)

LIBFDT_INC = $(CURDIR)/external/dtc/libfdt/
SYSREG_INC = $(CURDIR)/external/arm64-sysreg-lib/include

CFLAGS += \
	-I$(GNUEFI_DIR)/inc/ -I$(GNUEFI_DIR)/inc/$(ARCH) -I$(GNUEFI_DIR)/inc/protocol \
	-I$(SYSREG_INC) -I$(LIBFDT_INC) -Isrc/include \
	-fpic -fshort-wchar -fno-stack-protector -ffreestanding \
	-DCONFIG_$(ARCH) -D__MAKEWITH_GNUEFI -DGNU_EFI_USE_MS_ABI \
	-mstrict-align

LDFLAGS += \
	-Wl,--no-wchar-size-warning \
	-e efi_main \
	-s -Wl,-Bsymbolic -nostdlib -shared \
	-L $(GNUEFI_OUT)/lib/ -L $(GNUEFI_OUT)/gnuefi \

LIBS = \
	-lefi -lgnuefi \
	-T $(GNUEFI_DIR)/gnuefi/elf_$(ARCH)_efi.lds


LIBEFI_A 	:= $(GNUEFI_OUT)/lib/libefi.a
LIBGNUEFI_A 	:= $(GNUEFI_OUT)/gnuefi/libgnuefi.a
CRT0_O 		:= $(GNUEFI_OUT)/gnuefi/crt0-efi-$(ARCH).o

LIBFDT_OBJS := \
	$(OUT_DIR)/external/dtc/libfdt/fdt.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_ro.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_wip.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_sw.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_rw.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_empty_tree.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_addresses.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_check.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_overlay.o \
	$(OUT_DIR)/external/dtc/libfdt/fdt_strerror.o \

DTBHACK_LDFLAGS := \
	-Wl,--defsym=EFI_SUBSYSTEM=$(SUBSYSTEM_APP)

DTBHACK_OBJS := \
	$(OUT_DIR)/src/dtbhack_main.o \
	$(OUT_DIR)/src/util.o \
	$(OUT_DIR)/src/arch.o \
	$(OUT_DIR)/src/libc.o \
	$(LIBFDT_OBJS)

SLTEST_LDFLAGS := \
	-Wl,--defsym=EFI_SUBSYSTEM=$(SUBSYSTEM_APP)

SLTEST_OBJS := \
	$(OUT_DIR)/src/test_main.o \
	$(OUT_DIR)/src/util.o \
	$(OUT_DIR)/src/arch.o \
	$(OUT_DIR)/src/sl.o \
	$(OUT_DIR)/src/trans.o \

SLBOUNCE_LDFLAGS := \
	-Wl,--defsym=EFI_SUBSYSTEM=$(SUBSYSTEM_RT)

SLBOUNCE_OBJS := \
	$(OUT_DIR)/src/bounce_main.o \
	$(OUT_DIR)/src/util.o \
	$(OUT_DIR)/src/arch.o \
	$(OUT_DIR)/src/sl.o \
	$(OUT_DIR)/src/trans.o \

DTBS := \
	$(OUT_DIR)/dtbo/sc7180-symbols.dtbo \
	$(OUT_DIR)/dtbo/sc7180-el2.dtbo \

all: $(LIBEFI_A) $(LIBGNUEFI_A) $(OUT_DIR)/sltest.efi $(OUT_DIR)/slbounce.efi $(OUT_DIR)/dtbhack.efi

$(LIBEFI_A):
	@echo [ DEP ] $@
	ln -s /usr/include/elf.h $(GNUEFI_DIR)/inc/elf.h
	@$(MAKE) -C$(GNUEFI_DIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH) lib
	rm $(GNUEFI_DIR)/inc/elf.h

$(LIBGNUEFI_A):
	@echo [ DEP ] $@
	ln -s /usr/include/elf.h $(GNUEFI_DIR)/inc/elf.h
	@$(MAKE) -C$(GNUEFI_DIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH) gnuefi
	rm $(GNUEFI_DIR)/inc/elf.h

$(OUT_DIR)/sltest.so: $(SLTEST_OBJS)
	@echo [ LD  ] $$(basename $@)
	@$(CC) $(SLTEST_LDFLAGS) $(LDFLAGS) $(CRT0_O) $^ -o $@ $(LIBS)

$(OUT_DIR)/slbounce.so: $(SLBOUNCE_OBJS)
	@echo [ LD  ] $$(basename $@)
	@$(CC) $(SLBOUNCE_LDFLAGS) $(LDFLAGS) $(CRT0_O) $^ -o $@ $(LIBS)

$(OUT_DIR)/dtbhack.so: $(DTBHACK_OBJS)
	@echo [ LD  ] $$(basename $@)
	@$(CC) $(DTBHACK_LDFLAGS) $(LDFLAGS) $(CRT0_O) $^ -o $@ $(LIBS)

$(OUT_DIR)/%.efi: $(OUT_DIR)/%.so
	@echo [ CPY ] $$(basename $@)
	@$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel* \
	            -j .rela* -j .reloc -j .eh_frame -O binary $< $@

$(OUT_DIR)/%.o: %.c
	@echo [ CC  ] $$(basename $@)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: %.s
	@echo [ ASM ] $$(basename $@)
	@mkdir -p $(dir $@)
	@$(AS) -c $< -o $@

dtbs: $(DTBS)

$(OUT_DIR)/%.dtbo: %.dtso
	@echo [ DTC ] $$(basename $@)
	@mkdir -p $(dir $@)
	@$(DTC) -O dtb -I dts --align 16 -o $@ $<

.PHONY: clean
clean:
	rm -r $(OUT_DIR)
	$(MAKE) -C$(GNUEFI_DIR) ARCH=$(ARCH) clean

