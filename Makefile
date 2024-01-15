
ARCH 		:= aarch64
SUBSYSTEM 	:= 10
CROSS_COMPILE 	:= aarch64-linux-gnu-

AS             := $(CROSS_COMPILE)as
CC             := $(CROSS_COMPILE)gcc
LD             := $(CROSS_COMPILE)ld
OBJCOPY        := $(CROSS_COMPILE)objcopy

OUT_DIR := $(CURDIR)/out

GNUEFI_DIR = $(CURDIR)/external/gnu-efi
GNUEFI_OUT = $(GNUEFI_DIR)/$(ARCH)

SYSREG_INC = $(CURDIR)/external/arm64-sysreg-lib/include

CFLAGS += \
	-I$(GNUEFI_DIR)/inc/ -I$(GNUEFI_DIR)/inc/$(ARCH) -I$(GNUEFI_DIR)/inc/protocol \
	-I$(SYSREG_INC) \
	-fpic -fshort-wchar -fno-stack-protector -ffreestanding \
	-DCONFIG_$(ARCH) -D__MAKEWITH_GNUEFI -DGNU_EFI_USE_MS_ABI \
	-mstrict-align

LDFLAGS += \
	-Wl,--no-wchar-size-warning -Wl,--defsym=EFI_SUBSYSTEM=$(SUBSYSTEM) \
	-e efi_main \
	-s -Wl,-Bsymbolic -nostdlib -shared \
	-L $(GNUEFI_OUT)/lib/ -L $(GNUEFI_OUT)/gnuefi \

LIBS = \
	-lefi -lgnuefi \
	-T $(GNUEFI_DIR)/gnuefi/elf_$(ARCH)_efi.lds


LIBEFI_A 	:= $(GNUEFI_OUT)/lib/libefi.a
LIBGNUEFI_A 	:= $(GNUEFI_OUT)/gnuefi/libgnuefi.a
CRT0_O 		:= $(GNUEFI_OUT)/gnuefi/crt0-efi-$(ARCH).o

OBJS := \
	$(OUT_DIR)/main.o \
	$(OUT_DIR)/util.o \
	$(OUT_DIR)/arch.o \
	$(OUT_DIR)/sl.o \
	$(OUT_DIR)/tzlog.o \
	$(OUT_DIR)/tinyfb.o \
	$(OUT_DIR)/tpm.o \
	$(OUT_DIR)/tzapp.o \
	$(OUT_DIR)/smp.o \
	$(OUT_DIR)/smp_entry.o \

all: $(OUT_DIR) $(LIBEFI_A) $(LIBGNUEFI_A) $(OUT_DIR)/slbounce.efi

$(OUT_DIR):
	mkdir $@

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

$(OUT_DIR)/slbounce.so: $(OBJS)
	@echo [ LD  ] $$(basename $@)
	@$(CC) $(LDFLAGS) $(CRT0_O) $^ -o $@ $(LIBS)

$(OUT_DIR)/%.efi: $(OUT_DIR)/%.so
	@echo [ CPY ] $$(basename $@)
	@$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel* \
	            -j .rela* -j .reloc -j .eh_frame -O binary $< $@

$(OUT_DIR)/%.o: src/%.c
	@echo [ CC  ] $$(basename $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.o: src/%.s
	@echo [ ASM ] $$(basename $@)
	@$(AS) -c $< -o $@

.PHONY: clean
clean:
	rm -r $(OUT_DIR)
	$(MAKE) -C$(GNUEFI_DIR) ARCH=$(ARCH) clean

