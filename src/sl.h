#ifndef SL_H
#define SL_H

#include <stdint.h>
#include <efi.h>

#define __PACKED __attribute__((packed))

#define SMC_SL_ID	0xc3000001

enum sl_cmd {
	SL_CMD_IS_AVAILABLE	= 1,
	SL_CMD_AUTH		= 2,
	SL_CMD_RESERVE_MEM	= 3,
	SL_CMD_LAUNCH		= 4,
	SL_CMD_UNMAP_ALL	= 5,
};

struct sl_smc_params {
	uint16_t a;			// 1
	uint16_t b;			// 0
	uint32_t version;		// 0x10
	uint32_t num;			// id (1, 2, 4)
	uint32_t pad;			// 0

	uint64_t pe_data;
	uint64_t pe_size;
	uint64_t arg_data;
	uint64_t arg_size;
} __PACKED;

struct sl_dma_range {
	uint64_t a;
	uint64_t b;
	uint64_t c;
} __PACKED;

struct sl_tb_data {
	uint64_t mair;
	uint64_t sp;
	uint64_t tcr;
	uint64_t ttbr0;
	uint64_t ttbr1;
} __PACKED;

struct sl_tz_data {
	uint32_t version;		// 1

	uint32_t cert_offt;		// this_end + 0x18000 = 0x19000
	uint32_t cert_size;

	uint32_t tcg_offt;		// cert_offt + cert_size
	uint32_t tcg_size;		// 0x2000
	uint32_t tcg_used;		// 0
	uint32_t tcg_ver;		// 2
	uint32_t pad1;

	uint64_t this_size;		// total alloc size
	uint64_t this_phys;

	uint32_t crt_offt;		// 0x1000
	uint32_t crt_pages_cnt;		// 0x18

	uint64_t boot_params;
	uint32_t boot_params_size;
	uint32_t pad2;

	uint64_t tb_entry_point;
	uint64_t tb_virt;
	uint64_t tb_phys;
	uint64_t tb_size;

	// ptr to this is passed in to tb entry.
	struct sl_tb_data tb_data;

	struct sl_dma_range dma_ranges[2];
	uint32_t dma_ranges_cnt;
	uint32_t pad3;
} __PACKED;

#define SL_BOOT_PARAMS_SIG 0x50504120544f4f42	// 'BOOT APP'

/*
 * Based on ReactOS _BOOT_APPLICATION_PARAMETER_BLOCK
 */
struct sl_boot_params {
	/* This header tells the library what image we're dealing with */
	uint64_t Signature;			// 'BOOT APP' = 0x50504120544f4f42
	uint32_t Version;			// 2
	uint32_t Size;				// 0x7c + size of the entries.
	uint32_t ImageType;			// 0xaa64
	uint32_t MemoryTranslationType;		// 2 (??)

	/* Where is the image located */
	uint64_t ImageBase;
	uint32_t ImageSize;

	/* Offset to BL_MEMORY_DATA */
	uint32_t MemoryDataOffset;

	/* Offset to BL_APPLICATION_ENTRY */
	uint32_t AppEntryOffset;

	/* Offset to BL_DEVICE_DESCRPIPTOR */
	uint32_t BootDeviceOffset;

	/* Offset to BL_FIRMWARE_PARAMETERS */
	uint32_t FirmwareParametersOffset;

	/* Offset to BL_RETURN_ARGUMENTS */
	uint32_t ReturnArgumentsOffset;

	// Something about the memory map...
	uint32_t unk0;				// some offset
	uint32_t unk1;				// 1
	uint32_t unk2;				// 0x14
	uint32_t unk3;				// 0x30
	uint32_t unk4;				// cnt? / 0x30
	uint32_t unk5;				// 0x10
} __PACKED;

EFI_STATUS sl_bounce(EFI_FILE_HANDLE tcblaunch);

#endif
