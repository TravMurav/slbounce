// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <efi.h>
#include <efilib.h>

#include "util.h"

EFI_FILE_HANDLE GetVolume(EFI_HANDLE image)
{
	EFI_STATUS status;
	EFI_LOADED_IMAGE *loaded_image = NULL;                  /* image interface */
	EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      /* image interface GUID */
	EFI_FILE_IO_INTERFACE *IOVolume;                        /* file system interface */
	EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID; /* file system interface GUID */
	EFI_FILE_HANDLE Volume;                                 /* the volume's interface */

	/* get the loaded image protocol interface for our "image" */
	status = uefi_call_wrapper(BS->HandleProtocol, 3, image, &lipGuid, (void **) &loaded_image);
	if (EFI_ERROR(status))
		return NULL;

	/* get the volume handle */
	status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &fsGuid, (VOID*)&IOVolume);
	if (EFI_ERROR(status))
		return NULL;

	status = uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);
	if (EFI_ERROR(status))
		return NULL;

	return Volume;
}

EFI_FILE_HANDLE FileOpen(EFI_FILE_HANDLE Volume, CHAR16 *FileName)
{
	EFI_STATUS status;
	EFI_FILE_HANDLE     FileHandle;

	status = uefi_call_wrapper(Volume->Open, 5, Volume, &FileHandle, FileName,
				   EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
	if (EFI_ERROR(status))
		return NULL;

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
	EFI_STATUS status;

	status = uefi_call_wrapper(FileHandle->Read, 3, FileHandle, &ReadSize, Buffer);
	if (EFI_ERROR(status)) {
		Print(L"FileRead failed with %r (ReadSize=%d)\n", status, ReadSize);
		return 0;
	}

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

EFI_STATUS AllocateZeroPages(UINT64 page_count, EFI_PHYSICAL_ADDRESS *addr)
{
	EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, page_count, addr);

	if (!EFI_ERROR(status))
		ZeroMem(*(UINT8 **)addr, page_count * 4096);

	return status;
}

void FreePages(EFI_PHYSICAL_ADDRESS addr, UINT64 page_count)
{
	uefi_call_wrapper(BS->FreePages, 2, addr, page_count);
}
