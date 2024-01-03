// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2023 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include <sysreg/currentel.h>
#include <sysreg/ctr_el0.h>

#define SLP_MAGIC	0x0001000000000010
#define OEM_SMC_SLP	0xc3000001

/*
 * used with smc(0xc3000001, ptr, num, 0)
 */
struct secure_launch_msg {
	uint64_t magic;		// 0x0001000000000010
	uint32_t num;		// 1; 2; 4; 5
	uint32_t pad;		// 0
	uint64_t pe_base;	// something that is an NT header so probably a PE
	uint64_t pe_size;
	uint64_t data_base;	// Lots of data passed, like registers so maybe
	uint64_t data_size;	//    the PE launch argument
};

void clear_dcache_range(uint64_t start, uint64_t size)
{
	uint64_t cache_line_size = (1 << read_ctr_el0().dminline) * 4;
	uint64_t i, end = start + size;

	start = -cache_line_size & start;

	for (i = start; i < end; i += cache_line_size) {
		__asm__ volatile("dc civac, %0\n" : : "r" (i) :"memory");
	}
}

uint64_t smc(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3)
{
	register uint64_t r0 __asm__("r0") = x0;
	register uint64_t r1 __asm__("r1") = x1;
	register uint64_t r2 __asm__("r2") = x2;
	register uint64_t r3 __asm__("r3") = x3;

	__asm__ volatile(
		"smc	#0\n"
		: "+r" (r0) : : "r1", "r2", "r3"
	);
	return r0;
}

EFI_FILE_HANDLE GetVolume(EFI_HANDLE image)
{
	EFI_LOADED_IMAGE *loaded_image = NULL;                  /* image interface */
	EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      /* image interface GUID */
	EFI_FILE_IO_INTERFACE *IOVolume;                        /* file system interface */
	EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID; /* file system interface GUID */
	EFI_FILE_HANDLE Volume;                                 /* the volume's interface */

	/* get the loaded image protocol interface for our "image" */
	uefi_call_wrapper(BS->HandleProtocol, 3, image, &lipGuid, (void **) &loaded_image);
	/* get the volume handle */
	uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &fsGuid, (VOID*)&IOVolume);
	uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);
	return Volume;
}

EFI_FILE_HANDLE FileOpen(EFI_FILE_HANDLE Volume, CHAR16 *FileName)
{
	EFI_FILE_HANDLE     FileHandle;

	uefi_call_wrapper(Volume->Open, 5, Volume, &FileHandle, FileName, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
	return FileHandle;
}

UINT64 FileSize(EFI_FILE_HANDLE FileHandle)
{
	UINT64 ret;
	EFI_FILE_INFO       *FileInfo;         /* file information structure */
	/* get the file's size */
	FileInfo = LibFileInfo(FileHandle);
	ret = FileInfo->FileSize;
	FreePool(FileInfo);
	return ret;
}

UINT64 FileRead(EFI_FILE_HANDLE FileHandle, UINT8 *Buffer, UINT64 ReadSize)
{
	uefi_call_wrapper(FileHandle->Read, 3, FileHandle, &ReadSize, Buffer);
	return ReadSize;
}

void FileClose(EFI_FILE_HANDLE FileHandle)
{
	uefi_call_wrapper(FileHandle->Close, 1, FileHandle);
}

void WaitKey(EFI_SYSTEM_TABLE *SystemTable, int line)
{
	UINTN Event;

	//return;

	Print(L"(stall at line %d, press any key...)\n", line);

	SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);
	SystemTable->BootServices->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &Event);
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	struct secure_launch_msg *msg;
	EFI_FILE_HANDLE volume, file;
	UINT8 *app_buffer;
	UINT64 app_size;
	CHAR16 **argv;
	INTN argc;
	uint64_t ret;

	InitializeLib(ImageHandle, SystemTable);
	argc = GetShellArgcArgv(ImageHandle, &argv);

	Print(L"insecure-launch\n");
	Print(L"Running in EL=%d\n", read_currentel().el);
	uint64_t psci_version = smc(0x84000000, 0, 0, 0);
	Print(L"psci ret = 0x%x\n", psci_version);

	if (argc != 2) {
		Print(L"Usage: islp.efi tcblaunch.exe");
		return EFI_INVALID_PARAMETER;
	}

	Print(L"we are %s\n", argv[0]);
	Print(L"Launching using %s\n", argv[1]);

	volume = GetVolume(ImageHandle);
	file = FileOpen(volume, argv[1]);
	app_size = FileSize(file);
	app_buffer = AllocatePool(app_size);
	app_size = FileRead(file, app_buffer, app_size);

	msg = AllocatePool(sizeof(*msg));
	msg->magic = SLP_MAGIC;
	msg->num = 1;
	msg->pad = 0;
	msg->pe_base = 0;
	msg->pe_size = 0;
	msg->data_base = 0;
	msg->data_size = 0;


	WaitKey(SystemTable, __LINE__);
	clear_dcache_range((uint64_t)msg, sizeof(*msg));
	ret = smc(OEM_SMC_SLP, (uint64_t)msg, 1, 0);
	Print(L"SL Available: smc ret = 0x%x\n", ret);
	
	msg->pe_base = (uint64_t)app_buffer;
	msg->pe_size = app_size;
	msg->data_base = (uint64_t)AllocatePool(4096);
	msg->data_size = 4096;

	WaitKey(SystemTable, __LINE__);
	msg->num = 2;
	clear_dcache_range((uint64_t)msg, sizeof(*msg));
	ret = smc(OEM_SMC_SLP, (uint64_t)msg, 2, 0);
	Print(L"Authenticate: smc ret = 0x%x\n", ret);

	WaitKey(SystemTable, __LINE__);
	msg->num = 4;
	clear_dcache_range((uint64_t)msg, sizeof(*msg));
	ret = smc(OEM_SMC_SLP, (uint64_t)msg, 4, 0);
	Print(L"Launch: smc ret = 0x%x\n", ret);

	return EFI_SUCCESS;
}
