// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

#include <stdint.h>

#include <efi.h>
#include <efilib.h>

#include "Protocol/Tcg2Protocol.h"
#include "tpm.h"


struct efi_separator_tcg_event {
	EFI_TCG2_EVENT	EfiTcgEvent;
	UINT32		Value;
};

EFI_STATUS tpm_init(void)
{
	EFI_STATUS ret;

	EFI_TCG2_PROTOCOL *Tcg2Protocol;
	EFI_GUID gEfiTcg2ProtocolGuid = EFI_TCG2_PROTOCOL_GUID;

	ret = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiTcg2ProtocolGuid, NULL, (void**)&Tcg2Protocol);
	if (EFI_ERROR(ret)) {
		Print(L"Unable to locate TCG2 protocol.\n");
		return ret;
	}

	EFI_TCG2_BOOT_SERVICE_CAPABILITY ProtocolCapability;
	ProtocolCapability.Size = sizeof(ProtocolCapability);

	ret = uefi_call_wrapper(Tcg2Protocol->GetCapability, 2, Tcg2Protocol, &ProtocolCapability);
	if (EFI_ERROR(ret)) {
		Print(L"Unable to get TPM capabilities.\n");
		return ret;
	}

	Print(L"TCG2 Structure v%d.%d, Protocol v%d.%d\n",
			ProtocolCapability.StructureVersion.Major,
			ProtocolCapability.StructureVersion.Minor,
			ProtocolCapability.ProtocolVersion.Major,
			ProtocolCapability.ProtocolVersion.Minor);

	Print(L"Present = 0x%x, Vendor-ID: 0x%x\n",
			ProtocolCapability.TPMPresentFlag,
			ProtocolCapability.ManufacturerID);

	/* just hash something dumb */

	struct efi_separator_tcg_event SepEfiTcgEvent;
	SepEfiTcgEvent.EfiTcgEvent.Size = sizeof(SepEfiTcgEvent);
	SepEfiTcgEvent.EfiTcgEvent.Header.HeaderSize = sizeof(EFI_TCG2_EVENT_HEADER);
	SepEfiTcgEvent.EfiTcgEvent.Header.HeaderVersion = 1;
	SepEfiTcgEvent.EfiTcgEvent.Header.PCRIndex = 17;
	SepEfiTcgEvent.EfiTcgEvent.Header.EventType = EV_SEPARATOR;
	SepEfiTcgEvent.Value = 0xABABABAB;

	ret = uefi_call_wrapper(Tcg2Protocol->HashLogExtendEvent, 5, Tcg2Protocol,
				0, (EFI_PHYSICAL_ADDRESS)&ProtocolCapability, sizeof(ProtocolCapability),
				(EFI_TCG2_EVENT *)&SepEfiTcgEvent);
	if (EFI_ERROR(ret)) {
		Print(L"Unable to create an TPM separator event.\n");
		return ret;
	}

	SepEfiTcgEvent.EfiTcgEvent.Header.PCRIndex = 18;

	ret = uefi_call_wrapper(Tcg2Protocol->HashLogExtendEvent, 5, Tcg2Protocol,
				0, (EFI_PHYSICAL_ADDRESS)&ProtocolCapability, sizeof(ProtocolCapability),
				(EFI_TCG2_EVENT *)&SepEfiTcgEvent);
	if (EFI_ERROR(ret)) {
		Print(L"Unable to create an TPM separator event.\n");
		return ret;
	}


	return EFI_SUCCESS;
}
