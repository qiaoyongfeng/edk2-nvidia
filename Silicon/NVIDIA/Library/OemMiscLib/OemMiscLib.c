/** @file
*  OemMiscLib.c
*
*  Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <Uefi.h>
#include <ConfigurationManagerObject.h>
#include <PiDxe.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OemMiscLib.h>
#include <Library/PrintLib.h>
#include <Library/PlatformResourceLib.h>
#include <Library/TegraCpuFreqHelper.h>
#include <Library/FloorSweepingLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/TegraPlatformInfoLib.h>
#include <Library/DtPlatformDtbLoaderLib.h>
#include <Library/TimerLib.h>
#include <Library/HobLib.h>
#include <Protocol/Eeprom.h>
#include <Protocol/ConfigurationManagerDataProtocol.h>
#include <Protocol/ConfigurationManagerProtocol.h>
#include <libfdt.h>

#define LOWER_32(x)  ((UINT32)x)
#define UPPER_32(x)  ((UINT32)(x >> 32))
#define MAX_CNTR     (~0U)
#define REF_CLK_MHZ  (408)

STATIC TEGRA_EEPROM_BOARD_INFO  *SmEepromData;
STATIC SMBIOS_TABLE_TYPE32      *Type32Record;
STATIC SMBIOS_TABLE_TYPE3       *Type3Record;
STATIC CHAR16                   *BoardProductName;
STATIC CHAR16                   *AssetTag;
STATIC CHAR16                   *SerialNumber;
STATIC UINTN                    CurCpuFreqMhz;
STATIC UINT32                   SocketMask;

/**
  GetCpuFreqT194
  Helper function to compute CPU frequency for T194 done using T194 specific
  PMU counter registers.

  @param MiscProcessorData      Pointer to the Processor Data structure which
                                is used by the Smbios drivers.
                                it should be ignored by device driver.

  @retval                       CPU Frequency in Mhz.

**/
STATIC
UINT16
GetCpuFreqT194 (
  VOID
  )
{
  EFI_TPL  CurrentTpl;
  UINT64   BeginValue;
  UINT64   EndValue;
  UINT32   BeginCcnt;
  UINT32   BeginRefCnt;
  UINT32   EndCcnt;
  UINT32   EndRefCnt;
  UINT32   DeltaRefCnt;
  UINT32   DeltaCcnt;
  UINT32   FreqMHz;
  UINT16   ReturnFreq;

  CurrentTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  BeginValue = NvReadPmCntr ();
  MicroSecondDelay (100);
  EndValue = NvReadPmCntr ();
  gBS->RestoreTPL (CurrentTpl);

  BeginRefCnt = LOWER_32 (BeginValue);
  BeginCcnt   = UPPER_32 (BeginValue);
  EndRefCnt   = LOWER_32 (EndValue);
  EndCcnt     = UPPER_32 (EndValue);

  if (EndRefCnt < BeginRefCnt) {
    DeltaRefCnt = EndRefCnt + (MAX_CNTR - BeginRefCnt);
  } else {
    DeltaRefCnt = EndRefCnt - BeginRefCnt;
  }

  if (EndCcnt < BeginCcnt) {
    DeltaCcnt = EndCcnt + (MAX_CNTR - BeginCcnt);
  } else {
    DeltaCcnt = EndCcnt - BeginCcnt;
  }

  FreqMHz    = (DeltaCcnt * REF_CLK_MHZ) / DeltaRefCnt;
  ReturnFreq = (UINT16)FreqMHz;
  return ReturnFreq;
}

/**
  GetCpuFreqMhzFromCpcInfo
  Helper function to compute CPU frequency from CpcInfo CM Object.

  @param ProcessorIndex Index of the processor to get the frequency for.

  @retval                       CPU Frequency in Mhz.

**/
STATIC
UINTN
GetCpuFreqMhzFromCpcInfo (
  IN UINT8  ProcessorIndex
  )
{
  EFI_STATUS                            Status;
  EDKII_CONFIGURATION_MANAGER_PROTOCOL  *CfgMgrProtocol;
  CM_OBJ_DESCRIPTOR                     CmObjectDesc;
  CM_OBJ_DESCRIPTOR                     CmObjectDesc2;
  CM_ARM_GICC_INFO                      *GicCInfo;
  CM_ARM_CPC_INFO                       *CpcInfo;
  UINT32                                NumCores;
  UINT32                                Index;

  // Locate Configuration Manager Protocol for getting GicC and Cpc CM Objects.
  Status = gBS->LocateProtocol (
                  &gEdkiiConfigurationManagerProtocolGuid,
                  NULL,
                  (VOID **)&CfgMgrProtocol
                  );
  if ( EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Locate CfgMgrProtocol %r", __FUNCTION__, Status));
    return 0;
  }

  // Get GicC Info for each CPU core
  Status = CfgMgrProtocol->GetObject (
                             CfgMgrProtocol,
                             CREATE_CM_ARM_OBJECT_ID (EArmObjGicCInfo),
                             CM_NULL_TOKEN,
                             &CmObjectDesc
                             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Get GicC Info %r\n", __FUNCTION__, Status));
    return 0;
  }

  NumCores = CmObjectDesc.Count;
  GicCInfo = (CM_ARM_GICC_INFO *)CmObjectDesc.Data;

  for (Index = 0; Index < NumCores; Index++) {
    // Find the core which belongs to the ProcessorIndex
    if (GicCInfo[Index].ProximityDomain == ProcessorIndex) {
      if (GicCInfo[Index].CpcToken == CM_NULL_TOKEN) {
        DEBUG ((DEBUG_ERROR, "%a: CpcToken == CM_NULL_TOKEN\n", __FUNCTION__));
        return 0;
      }

      // Get the reference Cpc Info CM object for the core.
      Status = CfgMgrProtocol->GetObject (
                                 CfgMgrProtocol,
                                 CREATE_CM_ARM_OBJECT_ID (EArmObjCpcInfo),
                                 GicCInfo[Index].CpcToken,
                                 &CmObjectDesc2
                                 );
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "%a: Get Cpc Info %r\n", __FUNCTION__, Status));
        return 0;
      }

      CpcInfo = (CM_ARM_CPC_INFO *)CmObjectDesc2.Data;

      return CpcInfo->NominalFrequencyInteger;
    }
  }

  return 0;
}

/**
  GetCpuFreqMhz
  Helper Function to get current CPU Freq Cores. This is computed using
  PMU counter registers, currently this is done only for T194.

  @param ChipId                 SOC Id to compute Frequency for.
  @retval                       CPU Frequency in MHz

**/
STATIC
UINTN
GetCpuFreqMhz (
  UINTN  ChipId
  )
{
  UINTN  CpuFreqMhz = 0;

  if (ChipId == T194_CHIP_ID) {
    CpuFreqMhz = GetCpuFreqT194 ();
  } else {
    // Set it to zero for updating later by OemGetCpuFreq with ProcessorIndex.
    CpuFreqMhz = 0;
  }

  return CpuFreqMhz;
}

/**
  GetCpuEnabledCoresFromCm
  Helper function to get number of enabled cores from GicC CM Object.

  @param ProcessorIndex Index of the processor to get the frequency for.

  @retval               Number of enabled cores.

**/
STATIC
UINTN
GetCpuEnabledCoresFromCm (
  IN UINT8  ProcessorIndex
  )
{
  EFI_STATUS                            Status;
  EDKII_CONFIGURATION_MANAGER_PROTOCOL  *CfgMgrProtocol;
  CM_OBJ_DESCRIPTOR                     CmObjectDesc;
  CM_ARM_GICC_INFO                      *GicCInfo;
  UINT32                                TotalNumCores;
  UINT32                                NumCores;
  UINT32                                Index;

  // Locate Configuration Manager Protocol for getting GicC and Cpc CM Objects.
  Status = gBS->LocateProtocol (
                  &gEdkiiConfigurationManagerProtocolGuid,
                  NULL,
                  (VOID **)&CfgMgrProtocol
                  );
  if ( EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Locate CfgMgrProtocol %r", __FUNCTION__, Status));
    return 0;
  }

  // Get GicC Info for each CPU core
  Status = CfgMgrProtocol->GetObject (
                             CfgMgrProtocol,
                             CREATE_CM_ARM_OBJECT_ID (EArmObjGicCInfo),
                             CM_NULL_TOKEN,
                             &CmObjectDesc
                             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Get GicC Info %r\n", __FUNCTION__, Status));
    return 0;
  }

  NumCores      = 0;
  TotalNumCores = CmObjectDesc.Count;
  GicCInfo      = (CM_ARM_GICC_INFO *)CmObjectDesc.Data;

  for (Index = 0; Index < TotalNumCores; Index++) {
    // Find the core which belongs to the ProcessorIndex
    if (GicCInfo[Index].ProximityDomain == ProcessorIndex) {
      NumCores++;
    }
  }

  return NumCores;
}

/**
  PopulateCpuData
  Helper Function to get CPU/Core data.The Enabled Cores count
  is obtained using the Floorsweeping Library.

  @param ProcessorIndex         Index of the processor to get the CpuData for.
  @param MiscProcessorData      Pointer to the Processor Data structure which
                                is used by the Smbios drivers.
                                it should be ignored by device driver.

  @retval                       NONE

**/
STATIC
VOID
PopulateCpuData (
  IN UINT8                 ProcessorIndex,
  OEM_MISC_PROCESSOR_DATA  *MiscProcessorData
  )
{
  UINTN  CoresEnabled;

  MiscProcessorData->CurrentSpeed = CurCpuFreqMhz;
  MiscProcessorData->MaxSpeed     = CurCpuFreqMhz;
  CoresEnabled                    = GetCpuEnabledCoresFromCm (ProcessorIndex);
  MiscProcessorData->CoreCount    = CoresEnabled;
  MiscProcessorData->CoresEnabled = CoresEnabled;
  MiscProcessorData->ThreadCount  = 1;
}

/**
  PopulateCpuCharData
  Helper function to populate the CPU characteristics data. For
  now most of these are left hard-coded.

  @param ProcessorCharacteristics  Pointer to the Processor Characteristics
                                   data structure that will be used by the
                                   Smbios drivers.

  @retval                        NONE

**/
STATIC
VOID
PopulateCpuCharData (
  PROCESSOR_CHARACTERISTIC_FLAGS  *ProcessorCharacteristics
  )
{
  ProcessorCharacteristics->ProcessorReserved1              = 0;
  ProcessorCharacteristics->ProcessorUnknown                = 0;
  ProcessorCharacteristics->Processor64BitCapable           = 1;
  ProcessorCharacteristics->ProcessorMultiCore              = 0;
  ProcessorCharacteristics->ProcessorHardwareThread         = 0;
  ProcessorCharacteristics->ProcessorExecuteProtection      = 1;
  ProcessorCharacteristics->ProcessorEnhancedVirtualization = 0;
  ProcessorCharacteristics->ProcessorPowerPerformanceCtrl   = 0;
  ProcessorCharacteristics->Processor128BitCapable          = 0;
  ProcessorCharacteristics->ProcessorArm64SocId             = 1;
  ProcessorCharacteristics->ProcessorReserved2              = 0;
}

/**
  OemGetCpuFreq
  Gets the CPU frequency of the specified processor.

  @param ProcessorIndex Index of the processor to get the frequency for.

  @return               CPU frequency in Hz
**/
UINTN
EFIAPI
OemGetCpuFreq (
  IN UINT8  ProcessorIndex
  )
{
  UINTN  CpuFreqMhz = 0;

  if (CurCpuFreqMhz) {
    return (CurCpuFreqMhz * 1000 * 1000);
  }

  CpuFreqMhz = GetCpuFreqMhzFromCpcInfo (ProcessorIndex);

  return (CpuFreqMhz * 1000 * 1000);
}

/**
  OemGetProcessorInformation
  Gets information about the specified processor and stores it in
  the structures provided.

  @param ProcessorIndex  Index of the processor to get the information for.
  @param ProcessorStatus Processor status.
  @param ProcessorCharacteristics Processor characteritics.
  @param MiscProcessorData        Miscellaneous processor information.

  @return  TRUE on success, FALSE on failure.
**/
BOOLEAN
EFIAPI
OemGetProcessorInformation (
  IN UINTN                               ProcessorIndex,
  IN OUT PROCESSOR_STATUS_DATA           *ProcessorStatus,
  IN OUT PROCESSOR_CHARACTERISTIC_FLAGS  *ProcessorCharacteristics,
  IN OUT OEM_MISC_PROCESSOR_DATA         *MiscProcessorData
  )
{
  DEBUG ((DEBUG_INFO, "%a: ProcessorIndex %x ", __FUNCTION__, ProcessorIndex));

  if ((SocketMask & (1UL << ProcessorIndex)) != 0) {
    DEBUG ((DEBUG_INFO, "is enabled\n"));
    ProcessorStatus->Bits.CpuStatus       = 1; // CPU enabled
    ProcessorStatus->Bits.Reserved1       = 0;
    ProcessorStatus->Bits.SocketPopulated = 1;
    ProcessorStatus->Bits.Reserved2       = 0;
    PopulateCpuData (ProcessorIndex, MiscProcessorData);
    PopulateCpuCharData (ProcessorCharacteristics);
  } else {
    DEBUG ((DEBUG_INFO, "is disbled\n"));
    ProcessorStatus->Bits.CpuStatus       = 0; // CPU disabled
    ProcessorStatus->Bits.Reserved1       = 0;
    ProcessorStatus->Bits.SocketPopulated = 0;
    ProcessorStatus->Bits.Reserved2       = 0;
  }

  return TRUE;
}

/**
  GetMaxCacheLevels
  Gets the Max Cache Levels given the Configuration Manager (CM) Object for Caches.

  @param CmCacheObj    CM Object for Caches.

  @return              Max number of Cache levels.
**/
STATIC
UINTN
GetMaxCacheLevels (
  CM_OBJ_DESCRIPTOR  *CmCacheObj
  )
{
  UINTN  MaxCacheLevels = 0;

  if (CmCacheObj) {
    /*
     * HardCode this Logic for now, the assumption is that only L1
     * has a dedicated I/D Cache, all other levels have unified caches.
     */
    MaxCacheLevels = CmCacheObj->Count - 1;
  }

  return MaxCacheLevels;
}

/**
  GetCacheIndex
  Gets the Index into the CM_ARM_CACHE_INFO datastructure for the given CacheLevel.

  @param CmCacheObj    Max number of Cache levels present
         CacheLevel    Cache Level for which data is needed.
         DataCache     Is this a DataCache.

  @return              Cache Index on success OR (MaxCacheLevels + 1) on failure.
**/
STATIC
UINTN
GetCacheIndex (
  IN UINTN    MaxCacheLevels,
  IN UINT8    CacheLevel,
  IN BOOLEAN  DataCache
  )
{
  UINTN  CacheIndex = 0;

  if (CacheLevel > MaxCacheLevels) {
    CacheIndex = MaxCacheLevels + 1;
  } else {
    /*
       * HardCode this Logic for now, the assumption is that only L1
       * has a dedicated I/D Cache, all other levels have unified caches.
       */
    if (CacheLevel == 1) {
      if (DataCache) {
        CacheIndex = 0;
      } else {
        CacheIndex = 1;
      }
    } else {
      CacheIndex = MaxCacheLevels - 1;
    }
  }

  return CacheIndex;
}

/**
  GetCmCacheObject
  Gets the Configuration Manager(CM) Object for the Caches;

  @param CmCacheObject Output Pram with the CM Cache Object.

  @return              EFI_SUCCESS on success/ error  on failure .
**/
STATIC
EFI_STATUS
GetCmCacheObject (
  OUT CM_OBJ_DESCRIPTOR  *CmCacheObj
  )
{
  EFI_STATUS                            Status;
  EDKII_CONFIGURATION_MANAGER_PROTOCOL  *CfgMgrProtocol;

  Status = gBS->LocateProtocol (
                  &gEdkiiConfigurationManagerProtocolGuid,
                  NULL,
                  (VOID **)&CfgMgrProtocol
                  );
  if ( EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a:Failed to Locate Config Manager Protocol: %r",
      __FUNCTION__,
      Status
      ));
  } else {
    Status = CfgMgrProtocol->GetObject (
                               CfgMgrProtocol,
                               CREATE_CM_ARM_OBJECT_ID (EArmObjCacheInfo),
                               CM_NULL_TOKEN,
                               CmCacheObj
                               );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "ERROR: Failed to Get Cache Info. Status = %r\n",
        Status
        ));
    }
  }

  return Status;
}

/**
  OemGetCacheInformation
  Gets information about the cache at the specified cache level.

  @param ProcessorIndex The processor to get information for.
  @param CacheLevel The cache level to get information for.
  @param DataCache  Whether the cache is a data cache.
  @param UnifiedCache Whether the cache is a unified cache.
  @param SmbiosCacheTable The SMBIOS Type7 cache information structure.

  @return TRUE on success, FALSE on failure.
**/
BOOLEAN
EFIAPI
OemGetCacheInformation (
  IN UINT8                   ProcessorIndex,
  IN UINT8                   CacheLevel,
  IN BOOLEAN                 DataCache,
  IN BOOLEAN                 UnifiedCache,
  IN OUT SMBIOS_TABLE_TYPE7  *SmbiosCacheTable
  )
{
  UINT8              CacheDataIdx = 0;
  UINT32             WritePolicy;
  CM_ARM_CACHE_INFO  *CacheInfo;
  CM_OBJ_DESCRIPTOR  CmCacheObj;
  UINT8              NumCacheLevels;
  EFI_STATUS         Status;

  if ((SocketMask & (1UL << ProcessorIndex)) == 0) {
    return FALSE;
  }

  SmbiosCacheTable->CacheConfiguration = CacheLevel - 1;
  Status                               = GetCmCacheObject (&CmCacheObj);

  // Unknown operational mode
  if (EFI_ERROR (Status)) {
    SmbiosCacheTable->CacheConfiguration |= (3 << 8);
  } else {
    CacheInfo      = (CM_ARM_CACHE_INFO *)CmCacheObj.Data;
    NumCacheLevels = GetMaxCacheLevels (&CmCacheObj);
    CacheDataIdx   = GetCacheIndex (NumCacheLevels, CacheLevel, DataCache);
    if (CacheDataIdx > NumCacheLevels) {
      SmbiosCacheTable->CacheConfiguration |= (3 << 8);
    } else {
      WritePolicy = CacheInfo[CacheDataIdx].Attributes;
      WritePolicy = (WritePolicy >> 4) & 0x1;
      if (WritePolicy == EFI_ACPI_6_4_CACHE_ATTRIBUTES_WRITE_POLICY_WRITE_THROUGH) {
        SmbiosCacheTable->CacheConfiguration |= (0 << 8);
      } else if (WritePolicy == EFI_ACPI_6_4_CACHE_ATTRIBUTES_WRITE_POLICY_WRITE_BACK) {
        SmbiosCacheTable->CacheConfiguration |= (1 << 8);
      } else {
        SmbiosCacheTable->CacheConfiguration |= (3 << 8);
      }
    }
  }

  return TRUE;
}

/**
  OemGetMaxProcessors
  Gets the maximum number of processors supported by the platform.

  @return The maximum number of processors.
**/
UINT8
EFIAPI
OemGetMaxProcessors (
  VOID
  )
{
  UINTN  NumProcessorSockets;
  UINTN  Index;

  NumProcessorSockets = 0;

  for (Index = 0; Index < PcdGet32 (PcdTegraMaxSockets); Index++) {
    if ((SocketMask & (1UL << Index)) != 0) {
      NumProcessorSockets++;
    }
  }

  return NumProcessorSockets;
}

/**
  OemGetChassisType
  Gets the type of chassis for the system.

  @retval The type of the chassis.
**/
MISC_CHASSIS_TYPE
EFIAPI
OemGetChassisType (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->Type;
  } else {
    return MiscChassisTypeUnknown;
  }
}

/**
  OemIsProcessorPresent
  Returns whether the specified processor is present or not.

  @param ProcessIndex The processor index to check.

  @return TRUE is the processor is present, FALSE otherwise.
**/
BOOLEAN
EFIAPI
OemIsProcessorPresent (
  IN UINTN  ProcessorIndex
  )
{
  if ((SocketMask & (1UL << ProcessorIndex)) != 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
  OemGetProductName
  Get the Name of the current product from the DT.

  @param  NONE.

  @return Null terminated unicode string for the product name.
**/
STATIC
CHAR16 *
OemGetProductName (
  VOID
  )
{
  VOID        *DtbBase;
  UINTN       DtbSize;
  CONST VOID  *Property;
  INT32       Length;
  EFI_STATUS  Status;

  if (BoardProductName == NULL) {
    Status = DtPlatformLoadDtb (&DtbBase, &DtbSize);
    if (EFI_ERROR (Status)) {
      return NULL;
    }

    Property = fdt_getprop (DtbBase, 0, "model", &Length);
    if ((Property != NULL) && (Length != 0)) {
      BoardProductName = AllocateZeroPool (Length * sizeof (CHAR16));
      if (BoardProductName == NULL) {
        DEBUG ((DEBUG_ERROR, "%a: Out of Resources.\r\n", __FUNCTION__));
        return NULL;
      }

      AsciiStrToUnicodeStrS (
        Property,
        BoardProductName,
        (Length * sizeof (CHAR16))
        );
    }
  }

  return BoardProductName;
}

/**
  OemGetAssetTag
  Get the AssetTag of the current product from the EEPROM Info.
  This should match the tag physically present on the board.

  @param  EepromInfo   Data structure read from the EEPROM.

  @return Null terminated unicode string for the AssetTag.
**/
STATIC
CHAR16 *
OemGetAssetTag (
  TEGRA_EEPROM_BOARD_INFO  *EepromInfo
  )
{
  if (AssetTag == NULL) {
    UINTN  AssetTagLen = (TEGRA_PRODUCT_ID_LEN + 1);
    AssetTag = AllocateZeroPool (AssetTagLen * sizeof (CHAR16));
    if (AssetTag == NULL) {
      DEBUG ((DEBUG_ERROR, "%a: Out of Resources.\r\n", __FUNCTION__));
      return NULL;
    }

    AsciiStrToUnicodeStrS (
      EepromInfo->ProductId,
      AssetTag,
      (AssetTagLen * sizeof (CHAR16))
      );
  }

  return AssetTag;
}

/**
  OemGetSerialNumber
  Get the Serial Number of the current product.

  @param  EepromInfo   Data structure read from the EEPROM.

  @return Null terminated unicode string for the SerialNumber.
**/
STATIC
CHAR16 *
OemGetSerialNumber (
  TEGRA_EEPROM_BOARD_INFO  *EepromInfo
  )
{
  if (SerialNumber == NULL) {
    SerialNumber = AllocateZeroPool (TEGRA_SERIAL_NUM_LEN * sizeof (CHAR16));
    if (SerialNumber == NULL) {
      DEBUG ((DEBUG_ERROR, "%a: Out of Resources.\r\n", __FUNCTION__));
      return NULL;
    }

    AsciiStrToUnicodeStrS (
      EepromInfo->SerialNumber,
      SerialNumber,
      (TEGRA_SERIAL_NUM_LEN * sizeof (CHAR16))
      );
  }

  return SerialNumber;
}

/**
  OemGetSocketDesignation
  Get the Socket Designation of the Processor Index from the DT.

  @param Index The Processor Index.

  @return Null terminated unicode string for the SocketDesignation.
**/
STATIC
CHAR16 *
OemGetSocketDesignation (
  IN UINTN  Index
  )
{
  CHAR16      *SocketDesignation;
  VOID        *DtbBase;
  UINTN       DtbSize;
  CONST VOID  *Property;
  INT32       Length;
  EFI_STATUS  Status;
  CHAR8       Type4tNodeStr[] = "/firmware/smbios/type4@xx";
  INT32       NodeOffset;

  SocketDesignation = NULL;

  Status = DtPlatformLoadDtb (&DtbBase, &DtbSize);
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  AsciiSPrint (Type4tNodeStr, sizeof (Type4tNodeStr), "/firmware/smbios/type4@%u", Index);
  NodeOffset = fdt_path_offset (DtbBase, Type4tNodeStr);
  if (NodeOffset < 0) {
    return NULL;
  }

  Property = fdt_getprop (DtbBase, NodeOffset, "socket-designation", &Length);
  if ((Property != NULL) && (Length != 0)) {
    SocketDesignation = AllocateZeroPool (Length * sizeof (CHAR16));
    if (SocketDesignation == NULL) {
      DEBUG ((DEBUG_ERROR, "%a: Out of Resources.\r\n", __FUNCTION__));
      return NULL;
    }

    AsciiStrToUnicodeStrS (
      Property,
      SocketDesignation,
      (Length * sizeof (CHAR16))
      );
  }

  return SocketDesignation;
}

/**
  OemUpdateSmbiosInfo
  Updates the HII string for the specified field.

  @param HiiHandle     The HII handle.
  @param TokenToUpdate The string to update.
  @param Field         The field to get information about.
**/
VOID
EFIAPI
OemUpdateSmbiosInfo (
  IN EFI_HII_HANDLE                    HiiHandle,
  IN EFI_STRING_ID                     TokenToUpdate,
  IN OEM_MISC_SMBIOS_HII_STRING_FIELD  Field
  )
{
  CHAR16  *HiiString = NULL;

  switch (Field) {
    case SystemManufacturerType01:
      HiiString = (CHAR16 *)PcdGetPtr (PcdSystemManufacturer);
      break;
    case FamilyType01:
      HiiString = (CHAR16 *)PcdGetPtr (PcdSystemFamilyType);
      break;
    case SkuNumberType01:
      HiiString = (CHAR16 *)PcdGetPtr (PcdSystemSku);
      break;
    case AssetTagType03:
    case AssertTagType02:
      if (SmEepromData) {
        HiiString = OemGetAssetTag (SmEepromData);
      }

      break;
    case ChassisLocationType02:
      HiiString = (CHAR16 *)PcdGetPtr (PcdBoardChassisLocation);
      break;
    case BoardManufacturerType02:
      HiiString = (CHAR16 *)PcdGetPtr (PcdBoardManufacturer);
      break;
    case SerialNumType01:
    case SerialNumberType02:
      if (SmEepromData) {
        HiiString = OemGetSerialNumber (SmEepromData);
      }

      break;
    case ProductNameType02:
    case ProductNameType01:
      HiiString =  OemGetProductName ();
      break;
    case VersionType03:
      HiiString = (CHAR16 *)PcdGetPtr (PcdChassisVersion);
      break;
    case ManufacturerType03:
      HiiString = (CHAR16 *)PcdGetPtr (PcdChassisManufacturer);
      break;
    case SkuNumberType03:
      HiiString = (CHAR16 *)PcdGetPtr (PcdChassisSku);
      break;
    case SerialNumberType03:
      HiiString = (CHAR16 *)PcdGetPtr (PcdChassisSerialNumber);
      break;
    case ProcessorSocketDesType04_0 ... ProcessorSocketDesType04_15:
      ASSERT (SD_FIELD_TO_INDEX (Field) < PcdGet32 (PcdTegraMaxSockets));
      HiiString = OemGetSocketDesignation (SD_FIELD_TO_INDEX (Field));
      break;
    default:
      break;
  }

  if (HiiString != NULL) {
    HiiSetString (HiiHandle, TokenToUpdate, HiiString, NULL);
  }
}

/**
  OemGetBootStatus
  Fetches the Type 32 boot information status.

  @return Boot status.
**/
MISC_BOOT_INFORMATION_STATUS_DATA_TYPE
EFIAPI
OemGetBootStatus (
  VOID
  )
{
  if (Type32Record) {
    return Type32Record->BootStatus;
  } else {
    return BootInformationStatusNoError;
  }
}

/**
  OemGetChassisBootupState
  Fetches the chassis status when it was last booted.

 @return Chassis status.
**/
MISC_CHASSIS_STATE
EFIAPI
OemGetChassisBootupState (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->BootupState;
  } else {
    return ChassisStateUnknown;
  }
}

/**
  OemGetChassisPowerSupplyState
  Fetches the chassis power supply/supplies status when last booted.

 @return Chassis power supply/supplies status.
**/
MISC_CHASSIS_STATE
EFIAPI
OemGetChassisPowerSupplyState (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->PowerSupplyState;
  } else {
    return ChassisStateUnknown;
  }
}

/**
  OemGetChassisThermalState
  Fetches the chassis thermal status when last booted.

 @return Chassis thermal status.
**/
MISC_CHASSIS_STATE
EFIAPI
OemGetChassisThermalState (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->PowerSupplyState;
  } else {
    return ChassisStateUnknown;
  }
}

/**
  OemGetChassisSecurityStatus
  Fetches the chassis security status when last booted.

 @return Chassis security status.
**/
MISC_CHASSIS_SECURITY_STATE
EFIAPI
OemGetChassisSecurityStatus (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->SecurityStatus;
  } else {
    return ChassisSecurityStatusUnknown;
  }
}

/**
  OemGetChassisHeight
  Fetches the chassis height in RMUs (Rack Mount Units).

  @return The height of the chassis.
**/
UINT8
EFIAPI
OemGetChassisHeight (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->Height;
  } else {
    return 0;
  }
}

/**
  OemGetChassisNumPowerCords
  Fetches the number of power cords.

  @return The number of power cords.
**/
UINT8
EFIAPI
OemGetChassisNumPowerCords (
  VOID
  )
{
  if (Type3Record) {
    return Type3Record->NumberofPowerCords;
  } else {
    return 0;
  }
}

/**
  OemMiscLibConstructor
  The Constructor Function gets the Platform specific data which is installed
  by SOC specific Libraries

  @retval EFI_SUCCESS   The constructor always returns EFI_SUCCESS.

**/
EFI_STATUS
EFIAPI
OemMiscLibConstructor (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       ChipId;
  VOID        *Hob;

  Status = gBS->LocateProtocol (&gNVIDIACvmEepromProtocolGuid, NULL, (VOID **)&SmEepromData);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: SMBIOS: Failed to get Board Data protocol %r\n",
      __FUNCTION__,
      Status
      ));
    SmEepromData = NULL;
  }

  Hob = GetFirstGuidHob (&gNVIDIAPlatformResourceDataGuid);
  if ((Hob != NULL) &&
      (GET_GUID_HOB_DATA_SIZE (Hob) == sizeof (TEGRA_PLATFORM_RESOURCE_INFO)))
  {
    SocketMask = ((TEGRA_PLATFORM_RESOURCE_INFO *)GET_GUID_HOB_DATA (Hob))->SocketMask;
  } else {
    ASSERT (FALSE);
    SocketMask = 0x1;
  }

  DEBUG ((DEBUG_INFO, "%a: SocketMask = 0x%x\n", __FUNCTION__, SocketMask));

  ChipId        =  TegraGetChipID ();
  CurCpuFreqMhz =  GetCpuFreqMhz (ChipId);
  Type32Record  = (SMBIOS_TABLE_TYPE32 *)PcdGetPtr (PcdType32Info);
  Type3Record   = (SMBIOS_TABLE_TYPE3 *)PcdGetPtr (PcdType3Info);
  return EFI_SUCCESS;
}
