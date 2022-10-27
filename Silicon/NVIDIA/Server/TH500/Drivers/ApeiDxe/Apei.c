/** @file
*
*  Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include "Apei.h"

EFI_ACPI_TABLE_PROTOCOL  *AcpiTableProtocol;
RAS_FW_BUFFER            RasFwBufferInfo;

STATIC
RAS_PCIE_DPC_COMM_BUF_INFO  *NVIDIARasNsCommPcieDpcData = NULL;

/*
 * Setup the ARM defined SDEI table to enable SDEI support in the OS. SDEI can
 * be used as a notification mechanism for some error sources.
 */
STATIC
EFI_STATUS
SdeiSetupTable (
  )
{
  EFI_STATUS               Status = EFI_SUCCESS;
  EFI_ACPI_6_X_SDEI_TABLE  *SdeiTable;
  UINTN                    SdeiSize;
  UINT8                    Checksum;
  UINTN                    AcpiTableHandle;

  /* Allocate enough space for the header and error sources */
  SdeiSize  = sizeof (EFI_ACPI_6_X_SDEI_TABLE);
  SdeiTable = AllocateReservedZeroPool (SdeiSize);

  *SdeiTable = (EFI_ACPI_6_X_SDEI_TABLE) {
    .Header = {
      .Signature       = EFI_ACPI_6_X_SDEI_TABLE_SIGNATURE,
      .Length          = sizeof (EFI_ACPI_6_X_SDEI_TABLE),
      .Revision        = EFI_ACPI_6_X_SDEI_TABLE_REVISION,
      .OemId           = EFI_ACPI_OEM_ID,
      .OemTableId      = EFI_ACPI_OEM_TABLE_ID,
      .OemRevision     = EFI_ACPI_OEM_REVISION,
      .CreatorId       = EFI_ACPI_CREATOR_ID,
      .CreatorRevision = EFI_ACPI_CREATOR_REVISION
    }
  };

  Checksum                   = CalculateCheckSum8 ((UINT8 *)(SdeiTable), SdeiTable->Header.Length);
  SdeiTable->Header.Checksum = Checksum;

  Status = AcpiTableProtocol->InstallAcpiTable (
                                AcpiTableProtocol,
                                SdeiTable,
                                SdeiTable->Header.Length,
                                &AcpiTableHandle
                                );
  return Status;
}

/**
 * Entry point of the driver.
 *
 * @param ImageHandle     The image handle.
 * @param SystemTable     The system table.
**/
EFI_STATUS
EFIAPI
ApeiDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  VOID        *DtbBase;
  UINTN       DtbSize;
  INTN        NodeOffset;
  BOOLEAN     SkipSdei;
  BOOLEAN     SkipHest;
  BOOLEAN     SkipBert;
  BOOLEAN     SkipEinj;

  SkipSdei = FALSE;
  SkipHest = FALSE;
  SkipBert = FALSE;
  SkipEinj = FALSE;

  Status = DtPlatformLoadDtb (&DtbBase, &DtbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NodeOffset = fdt_path_offset (DtbBase, "/firmware/uefi");
  if (NodeOffset >= 0) {
    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-sdei-table", NULL)) {
      SkipSdei = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip SDEI Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-hest-table", NULL)) {
      SkipHest = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip HEST Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-bert-table", NULL)) {
      SkipBert = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip BERT Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-einj-table", NULL)) {
      SkipEinj = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip EINJ Table\r\n", __FUNCTION__));
    }
  }

  ZeroMem ((UINT8 *)&RasFwBufferInfo, sizeof (RAS_FW_BUFFER));

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTableProtocol
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!SkipSdei) {
    Status = SdeiSetupTable ();
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = FfaGetRasFwBuffer (&RasFwBufferInfo);
  if (!EFI_ERROR (Status)) {
    gDS->AddMemorySpace (
           EfiGcdMemoryTypeReserved,
           RasFwBufferInfo.Base,
           RasFwBufferInfo.Size,
           EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
           );

    gDS->SetMemorySpaceAttributes (
           RasFwBufferInfo.Base,
           RasFwBufferInfo.Size,
           EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
           );

    NVIDIARasNsCommPcieDpcData = (RAS_PCIE_DPC_COMM_BUF_INFO *)AllocateZeroPool (sizeof (RAS_PCIE_DPC_COMM_BUF_INFO));
    if (NVIDIARasNsCommPcieDpcData == NULL) {
      DEBUG ((
        DEBUG_ERROR,
        "%a: RAS_FW NS Memory allocation for NVIDIARasNsCommPcieDpcData failed\r\n",
        __FUNCTION__
        ));
      return EFI_OUT_OF_RESOURCES;
    }

    Status = HestBertSetupTables (&RasFwBufferInfo, SkipHest, SkipBert);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (!SkipEinj) {
      Status = EinjSetupTable (&RasFwBufferInfo);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }

    NVIDIARasNsCommPcieDpcData->PcieBase = RasFwBufferInfo.PcieBase;
    NVIDIARasNsCommPcieDpcData->PcieSize = RasFwBufferInfo.PcieSize;
  } else {
    DEBUG ((
      EFI_D_ERROR,
      "%a: Failed to get RAS_FW NS shared mem: %r\n",
      __FUNCTION__,
      Status
      ));
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gNVIDIARasNsCommPcieDpcDataProtocolGuid,
                  (VOID *)NVIDIARasNsCommPcieDpcData,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Unable to install NVIDIARasNsCommPcieDpcDataProtocol (%r)\r\n", __FUNCTION__, Status));
    return EFI_PROTOCOL_ERROR;
  }

  DEBUG ((DEBUG_VERBOSE, "%a: Successfully installed NVIDIARasNsCommPcieDpcDataProtocol (%r)\r\n", __FUNCTION__, Status));

  return Status;
}
