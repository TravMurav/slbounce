// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include <sysreg/currentel.h>

#include "util.h"
#include "arch.h"
#include "sl.h"

EFI_STATUS sl_test(EFI_FILE_HANDLE tcblaunch, EFI_HANDLE ImageHandle)
{
	EFI_STATUS ret = EFI_SUCCESS;
	uint64_t smcret = 0;
	uint64_t pe_data, pe_size, arg_data, arg_size;
	struct sl_smc_params *smc_data;
	struct sl_tz_data *tz_data;

	ret = sl_create_data(tcblaunch, &smc_data, &pe_data, &pe_size, &arg_data, &arg_size);
	if (EFI_ERROR(ret)) {
		Print(L"Failed to prepare data for Secure-Launch: %d\n", ret);
		return ret;
	}

	/*
	 * To make sure we can run code after EBS, pass the framebuffer
	 * to the test code.
	 */

	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	ret = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
	if(EFI_ERROR(ret)) {
		Print(L"Unable to locate GOP\n");
		return ret;
	}

	tz_data = (struct sl_tz_data *)arg_data;
	tz_data->tb_data.sp  = gop->Mode->FrameBufferBase;		// base
	tz_data->tb_data.tcr = gop->Mode->Info->HorizontalResolution;	// stride

	Print(L"Data creation is done. Trying to perform Secure-Launch...\n");

	Print(L" == Available: ");
	smcret = sl_smc(smc_data, SL_CMD_IS_AVAILABLE, pe_data, pe_size, arg_data, arg_size);
	Print(L"0x%x\n", smcret);
	if (smcret) {
		Print(L"This device does not support Secure-Launch.\n");
		return EFI_UNSUPPORTED;
	}

	/*
	 * From this point onward it's not safe to return to UEFI
	 * unless we succeed. If the hyp encounters an error, and
	 * we are lucky enough for it to return to us, we will be
	 * left with some memory mapped into EL2, which we can't
	 * ever touch again.
	 */

	Print(L" == Auth: ");
	smcret = sl_smc(smc_data, SL_CMD_AUTH, pe_data, pe_size, arg_data, arg_size);
	Print(L"0x%x\n", smcret);
	if (smcret)
		goto exit_corrupted;

	Print(L" == Launch: ");

	/*
	 * If we return from Secure-Launch in EL2 while UEFI
	 * Boot Services are still available, we have a really
	 * high chance of crashing the system. This is likely
	 * caused by some hardware trying to  DMA access memory
	 * after the iommu settings were changed. Exiting BS
	 * helps us work around this.
	 */

	UINTN MemoryMapSize = 1024*512, MapKey, DescriptorSize;
	EFI_MEMORY_DESCRIPTOR *MemoryMap = AllocatePool(MemoryMapSize);
	UINT32 DescriptorVersion;
	ret = uefi_call_wrapper(BS->GetMemoryMap, 6, &MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
	ret = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);

	/* We set a special longjmp point here in hopes SL gets us back. */
	if (tb_setjmp(tb_jmp_buf) == 0) {
		smcret = sl_smc(smc_data, SL_CMD_LAUNCH, pe_data, pe_size, arg_data, arg_size);
		if (smcret)
			psci_reboot(); // Indicate a fatal error with a reboot.
	}

	/*
	 * We just turn the device off here since it's the most
	 * reliable way to assert that we got to this code.
	 */
	psci_off();

	return EFI_SUCCESS;

exit_corrupted:
	Print(L"=============================================\n");
	Print(L"       SMC failed with ret = 0x%x\n", smcret);
	Print(L" Assuming this system is in corrupted state!\n");
	Print(L"                Halting now.\n");
	Print(L"=============================================\n");

	/* Everything is probably messed up if we got here, force the user to reboot. */
	while(1)
		;

	return EFI_UNSUPPORTED;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_FILE_HANDLE volume, file;
	CHAR16 **argv;
	INTN argc;
	EFI_STATUS ret;

	InitializeLib(ImageHandle, SystemTable);
	argc = GetShellArgcArgv(ImageHandle, &argv);

	Print(L"SL-Bounce\n");
	Print(L"Running in EL=%d\n", read_currentel().el);

	if (read_currentel().el != 1) {
		Print(L"Already in EL2!\n\n");
		return EFI_SUCCESS;
	}

	if (argc != 2) {
		Print(L"Usage: slbounce.efi tcblaunch.exe\n\n");
		return EFI_INVALID_PARAMETER;
	}

	Print(L"We are %s\n", argv[0]);
	Print(L"Launching using %s\n", argv[1]);

	volume = GetVolume(ImageHandle);
	if (!volume) {
		Print(L"Getting volume failed.\n");
		return EFI_INVALID_PARAMETER;
	}

	file = FileOpen(volume, argv[1]);
	if (!file) {
		Print(L"Opening file failed.\n");
		return EFI_INVALID_PARAMETER;
	}

	ret = sl_test(file, ImageHandle);
	if (EFI_ERROR(ret))
		Print(L"Bounce failed with %d\n", ret);

	Print(L"Running in EL=%d\n", read_currentel().el);

	if (read_currentel().el == 2) {
		Print(L"Successfully switched to EL2, the firmware may be unstable!\n");
	}

	return ret;
}
