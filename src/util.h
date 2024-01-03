#ifndef UTIL_H
#define UTIL_H

EFI_FILE_HANDLE GetVolume(EFI_HANDLE image);
EFI_FILE_HANDLE FileOpen(EFI_FILE_HANDLE Volume, CHAR16 *FileName);
UINT64 FileSize(EFI_FILE_HANDLE FileHandle);
UINT64 FileRead(EFI_FILE_HANDLE FileHandle, UINT8 *Buffer, UINT64 ReadSize);
void FileClose(EFI_FILE_HANDLE FileHandle);
void WaitKey(EFI_SYSTEM_TABLE *SystemTable, int line);

#endif
