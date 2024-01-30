// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include <sysreg/currentel.h>

#include "util.h"
#include "arch.h"
#include "sl.h"

static struct sl_smc_params *smc_data;
static uint64_t pe_data, pe_size, arg_data, arg_size;

EFI_EXIT_BOOT_SERVICES real_ExitBootServices;

EFI_STATUS sl_ExitBootServices(EFI_HANDLE ImageHandle, UINTN MapKey)
{
	uint64_t smcret = 0;

	EFI_STATUS status = uefi_call_wrapper(real_ExitBootServices, 2, ImageHandle, MapKey);
	if (EFI_ERROR(status))
		return status;

	/* We set a special longjmp point here in hopes SL gets us back. */
	if (tb_setjmp(tb_jmp_buf) == 0) {
		clear_dcache_range((uint64_t)tb_jmp_buf, 8*21);
		smcret = sl_smc(smc_data, SL_CMD_LAUNCH, pe_data, pe_size, arg_data, arg_size);
		if (smcret)
			psci_reboot(); /* Indicate a fatal error with a reboot. */
	}

	return status;
}

EFI_STATUS sl_install(EFI_FILE_HANDLE tcblaunch)
{
	EFI_STATUS ret = EFI_SUCCESS;
	uint64_t smcret = 0;

	ret = sl_create_data(tcblaunch, &smc_data, &pe_data, &pe_size, &arg_data, &arg_size);
	if (EFI_ERROR(ret)) {
		Print(L"Failed to prepare data for Secure-Launch: %d\n", ret);
		return ret;
	}

	Print(L"Data creation is done. Trying to prepare Secure-Launch...\n");

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

	/*
	 * We can't just install a handler for EBS signal since
	 * we're not guaranteed to be the last code to run.Thus
	 * we install our hook into EBS to run SL right after
	 * the real ExitBootServices() returns.
	 */
	real_ExitBootServices = BS->ExitBootServices;
	BS->ExitBootServices = sl_ExitBootServices;

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
		return EFI_UNSUPPORTED;
	}

	volume = GetVolume(ImageHandle);
	if (!volume) {
		Print(L"Getting volume failed.\n");
		return EFI_INVALID_PARAMETER;
	}

	file = FileOpen(volume, L"tcblaunch.exe");
	if (!file) {
		Print(L"Opening file failed.\n");
		return EFI_INVALID_PARAMETER;
	}

	ret = sl_install(file);
	if (EFI_ERROR(ret)) {
		Print(L"Installing SL hook failed with %d\n", ret);
		return ret;
	}

	Print(L"=================================================\n");
	Print(L" BS->ExitBootServices() was replaced with a hook\n");
	Print(L"  that would perform Secure-Launch right after\n");
	Print(L"exiting UEFI. Your system will crash if SL fails.\n");
	Print(L"=================================================\n");

	return ret;
}
