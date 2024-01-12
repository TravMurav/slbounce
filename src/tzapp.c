// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */


#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include "util.h"
#include "arch.h"
#include "tzlog.h"
#include "sl.h"
#include "tzapp.h"

struct imgauth_data {
	uint32_t	cmd;
	uint32_t	status;

	uint64_t	pe_data;
	uint32_t	pe_size;
	uint32_t	pad2;
	uint64_t	arg_data;
	uint32_t	arg_size;
	uint32_t	pad3;
} __PACKED;


EFI_STATUS tzapp_poke()
{
	EFI_STATUS ret;
	EFI_PHYSICAL_ADDRESS buf, retbuf;
	struct smc_ret data = {0};
	uint64_t smcret;

	Print(L"Poking mssecapp\n");

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiReservedMemoryType, 1, &buf);
	if (EFI_ERROR(ret))
		return ret;

	ret = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiReservedMemoryType, 1, &retbuf);
	if (EFI_ERROR(ret))
		return ret;

	CopyMem((UINT8*)buf, "qcom.tz.mssecapp", 16);
	clear_dcache_range(buf, 4096);

	smcret = smc_ret(&data, 0x32000103, 0x00000022, buf, 16, 0, 0, 0);
	Print(L"Lookup: ret=0x%x unk=0x%x type=0x%x id=0x%x, [0x%x 0x%x 0x%x]\n",
		data.x0, data.x1, data.x2, data.x3, data.x4, data.x5, data.x6);

	uint64_t app_id = data.x3;

	SetMem((UINT8*)buf, 4096, 0);
	SetMem((UINT8*)retbuf, 4096, 0);

	struct imgauth_data *req = (void*)buf;
	struct imgauth_data *resp = (void*)retbuf;

	req->cmd = 0x1001;

	clear_dcache_range(buf, 4096);
	clear_dcache_range(retbuf, 4096);

	smcret = smc_ret(&data, 0x30000001, 0x00000885, app_id, buf, 0x28, retbuf, 0x40);
	Print(L"Lookup: ret=0x%x unk=0x%x type=0x%x id=0x%x, [0x%x 0x%x 0x%x]\n",
		data.x0, data.x1, data.x2, data.x3, data.x4, data.x5, data.x6);

	Print(L"resp: cmd=0x%x, resp=0x%x\n", resp->cmd, resp->status);

	return EFI_SUCCESS;
}
