// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#define EFI_DEBUG 1

#include <efi.h>
#include <efilib.h>
#include <efidebug.h>

#include <libfdt.h>

#include "util.h"
#include "arch.h"

static EFI_STATUS dtbhack_cmd_db_relocation(UINT8 *dtb)
{
	EFI_STATUS status;
	uint32_t offset;
	int ret;

	offset = fdt_node_offset_by_compatible(dtb, 0, "qcom,cmd-db");
	if (offset <= 0) {
		Print(L"Failed to find cmd-db node: %d\n", offset);
		return EFI_UNSUPPORTED;
	}

	const fdt32_t *cmd_db_reg = fdt_getprop(dtb, offset, "reg", &ret);
	ASSERT(ret == 4 * 4);

	uint64_t cmd_db_base = ((uint64_t)fdt32_to_cpu(cmd_db_reg[0]) << 32) | fdt32_to_cpu(cmd_db_reg[1]);
	uint64_t cmd_db_size = ((uint64_t)fdt32_to_cpu(cmd_db_reg[2]) << 32) | fdt32_to_cpu(cmd_db_reg[3]);
	ASSERT(cmd_db_base);
	ASSERT(cmd_db_size);

	ret = fdt_nop_property(dtb, offset, "compatible");
	ASSERT(ret >= 0);

	EFI_PHYSICAL_ADDRESS cmddb_phys = 0;
	UINT64 cmddb_pages = cmd_db_size / 4096 + 1;

	status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiReservedMemoryType, cmddb_pages, &cmddb_phys);
	if (EFI_ERROR(status)) {
		Print(L"Failed to allocate memory: %d\n", status);
		return status;
	}
	Print(L"Relocating cmd-db: reg=0x%llx size=0x%llx new_addr=0x%llx\n", cmd_db_base, cmd_db_size, cmddb_phys);

	CopyMem((UINT8*)cmddb_phys, (UINT8*)cmd_db_base, cmd_db_size);

	int resmem_offset = fdt_path_offset(dtb, "/reserved-memory");
	if (resmem_offset <= 0)
		goto error_allocated;

	offset = fdt_add_subnode(dtb, resmem_offset, "cmd-db-copy");
	if (offset <= 0)
		goto error_allocated;

	ret = fdt_setprop_string(dtb, offset, "compatible", "qcom,cmd-db");
	if (ret)
		goto error_allocated;

	ret = fdt_appendprop_addrrange(dtb, resmem_offset, offset, "reg", cmddb_phys, cmd_db_size);
	if (ret)
		goto error_allocated;

	ret = fdt_setprop_empty(dtb, offset, "no-map");
	if (ret)
		goto error_allocated;

	return EFI_SUCCESS;

error_allocated:
	uefi_call_wrapper(BS->FreePages, 2, cmddb_phys, cmddb_pages);
	return EFI_UNSUPPORTED;
}

static EFI_STATUS dtbhack_zap_zap_shader(UINT8 *dtb)
{
	uint32_t offset;
	int ret;

	offset = fdt_node_offset_by_compatible(dtb, 0, "qcom,adreno");
	if (offset <= 0) {
		Print(L"Failed to find adreno node: %d\n", offset);
		return EFI_UNSUPPORTED;
	}

	offset = fdt_subnode_offset(dtb, offset, "zap-shader");
	if (offset <= 0) {
		Print(L"Failed to find gpu/zap-shader node: %d\n", offset);
		return EFI_UNSUPPORTED;
	}

	ret = fdt_nop_node(dtb, offset);
	if (ret) {
		Print(L"Failed to nop gpu/zap-shader node: %d\n", ret);
		return EFI_UNSUPPORTED;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS dtbhack_sc7180(UINT8 *dtb)
{
	EFI_STATUS status;

	/*
	 * cmd-db memory is for some reason "broken" after switching to el2.
	 * Let's make a copy in another place for fun and give linux that.
	 */
	status = dtbhack_cmd_db_relocation(dtb);
	if (EFI_ERROR(status)) {
		Print(L"Failed to relocate cmd-db: %d\n", status);
		return status;
	}

	return status;
}

static EFI_STATUS dtbhack_sc8280xp(UINT8 *dtb)
{
	EFI_STATUS status;

	/* No soc-specific workarounds just yet. Left TODO. */
	status = EFI_SUCCESS;

	return status;
}

#define EFI_DTB_TABLE_GUID \
    { 0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0} }

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	CHAR16 **argv;
	INTN argc;
	EFI_STATUS status;
	int ret;

	InitializeLib(ImageHandle, SystemTable);
	argc = GetShellArgcArgv(ImageHandle, &argv);

	Print(L"DTB-Hack\n");

	if (argc != 2) {
		Print(L"Usage: dtbhack.efi DTB\n\n");
		return EFI_INVALID_PARAMETER;
	}

	CHAR16 *dtb_name = argv[1];

	Print(L"Installing DTB: %s\n", dtb_name);

	EFI_FILE_HANDLE volume = GetVolume(ImageHandle);
	if (!volume) {
		Print(L"Cant open volume\n");
		return EFI_INVALID_PARAMETER;
	}

	EFI_FILE_HANDLE dtb_file = FileOpen(volume, dtb_name);
	if (!dtb_file) {
		Print(L"Cant open the file\n");
		return EFI_INVALID_PARAMETER;
	}

	EFI_PHYSICAL_ADDRESS dtb_phys;
	UINT64 dtb_max_sz = 1 * 1024 * 1024;
	UINT64 dtb_pages  = dtb_max_sz / 4096;

	/* The spec mandates using "ACPI" memory type for any configuration tables like dtb */
	status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiACPIReclaimMemory, dtb_pages, &dtb_phys);
	if (EFI_ERROR(status)) {
		Print(L"Failed to allocate memory: %d\n", status);
		return status;
	}

	UINT8 *dtb    = (UINT8 *)(dtb_phys);
	UINT64 dtb_sz = FileSize(dtb_file);

	if (dtb_sz > 1 * 1024 * 1024) {
		Print(L"File too big!\n");
		status = EFI_BUFFER_TOO_SMALL;
		goto error_allocated;
	}

	FileRead(dtb_file, dtb, dtb_sz);

	/*
	 * Now we need to update the DTB to make it usable.
	 */

	ret = fdt_check_header(dtb);
	if (ret) {
		Print(L"fdt header check failed: %d\n", ret);
		status = EFI_LOAD_ERROR;
		goto error_allocated;
	}

	ret = fdt_open_into(dtb, dtb, dtb_max_sz);
	if (ret) {
		Print(L"fdt open failed: %d\n", ret);
		status = EFI_LOAD_ERROR;
		goto error_allocated;
	}

	/*
	 * SoC-specific updates.
	 */

	if (fdt_node_check_compatible(dtb, 0, "qcom,sc7180")) {
		status = dtbhack_sc7180(dtb);
	} else if (fdt_node_check_compatible(dtb, 0, "qcom,sc8280xp")) {
		status = dtbhack_sc8280xp(dtb);
	} else {
		Print(L"NOTE: No soc-specific updates done.\n");
	}

	if (EFI_ERROR(status)) {
		Print(L"Failed to apply soc-specific updates: %d\n", status);
		goto error_allocated;
	}

	/*
	 * Generic updates.
	 */

	/*
	 * Since we are going to run in EL2, the hyp that would protect zap
	 * shader is gone. We also seem to be able to just ignore it in EL2
	 * since we now have the access to the needed registers.
	 */
	status = dtbhack_zap_zap_shader(dtb);
	if (EFI_ERROR(status)) {
		Print(L"Failed to nop-out zap shader: %d\n", status);
		goto error_allocated;
	}

	ret = fdt_pack(dtb);
	if (ret) {
		Print(L"fdt pack failed: %d\n", ret);
		status = EFI_LOAD_ERROR;
		goto error_allocated;
	}

	clear_dcache_range((uint64_t)dtb, dtb_max_sz);

	/*
	 * Finally, we need to install the dtb into a UEFI table so
	 * the OS can find it.
	 */

	EFI_GUID EfiDtbTableGuid = EFI_DTB_TABLE_GUID;

	status = uefi_call_wrapper(BS->InstallConfigurationTable, 2, &EfiDtbTableGuid, dtb);
	if (EFI_ERROR(status)) {
		Print(L"Failed to install dtb: %d\n", status);
		goto error_allocated;
	}

	Print(L"The DTB configuration table was installed!\n");

	return EFI_SUCCESS;

error_allocated:
	uefi_call_wrapper(BS->FreePages, 2, dtb_phys, dtb_pages);
	return ret;
}

