// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include <sysreg/currentel.h>

#include "util.h"
#include "sl.h"

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

	ret = sl_bounce(file);
	if (EFI_ERROR(ret))
		Print(L"Bounce failed with %d\n", ret);

	Print(L"Running in EL=%d\n", read_currentel().el);

	if (read_currentel().el == 2) {
		Print(L"Successfully switched to EL2, the firmware may be unstable!\n");
	}

	return ret;
}
