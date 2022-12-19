/** @file
  Configuration Manager Data Dxe

  Copyright (c) 2019 - 2022, NVIDIA Corporation. All rights reserved.
  Copyright (c) 2017 - 2018, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
    - Cm or CM   - Configuration Manager
    - Obj or OBJ - Object
**/

#include <ConfigurationManagerDataDxePrivate.h>

// Platform CPU configuration
#define PLATFORM_MAX_CORES_PER_CLUSTER  (PcdGet32 (PcdTegraMaxCoresPerCluster))
#define PLATFORM_MAX_CLUSTERS           (PcdGet32 (PcdTegraMaxClusters))
#define PLATFORM_MAX_CPUS               (PLATFORM_MAX_CLUSTERS * \
                                         PLATFORM_MAX_CORES_PER_CLUSTER)

/** The platform configuration repository information.
*/
STATIC
EDKII_PLATFORM_REPOSITORY_INFO  *NVIDIAPlatformRepositoryInfo;

// AML Patch protocol
NVIDIA_AML_PATCH_PROTOCOL  *PatchProtocol = NULL;

STATIC EFI_ACPI_DESCRIPTION_HEADER  *AcpiTableArray[] = {
  (EFI_ACPI_DESCRIPTION_HEADER *)dsdt_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)ssdtsocket1_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)ssdtsocket2_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)ssdtsocket3_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket0_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket1_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket2_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket3_aml_code
};

STATIC AML_OFFSET_TABLE_ENTRY  *OffsetTableArray[] = {
  DSDT_TH500_OffsetTable,
  SSDT_TH500_S1_OffsetTable,
  SSDT_TH500_S2_OffsetTable,
  SSDT_TH500_S3_OffsetTable,
  SSDT_BPMP_S0_OffsetTable,
  SSDT_BPMP_S1_OffsetTable,
  SSDT_BPMP_S2_OffsetTable,
  SSDT_BPMP_S3_OffsetTable
};

STATIC EFI_ACPI_DESCRIPTION_HEADER  *AcpiBpmpTableArray[] = {
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket0_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket1_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket2_aml_code,
  (EFI_ACPI_DESCRIPTION_HEADER *)bpmpssdtsocket3_aml_code
};

/** The platform configuration manager information.
*/
STATIC
CM_STD_OBJ_CONFIGURATION_MANAGER_INFO  CmInfo = {
  CONFIGURATION_MANAGER_REVISION,
  CFG_MGR_OEM_ID
};

/** The platform ACPI table list.
*/
STATIC
CM_STD_OBJ_ACPI_TABLE_INFO  CmAcpiTableList[] = {
  // FADT Table
  {
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdFadt),
    NULL,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision)
  },
  // GTDT Table
  {
    EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdGtdt),
    NULL,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision)
  },
  // MADT Table
  {
    EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdMadt),
    NULL,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision)
  },
  // DSDT Table
  {
    EFI_ACPI_6_4_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdDsdt),
    (EFI_ACPI_DESCRIPTION_HEADER *)dsdt_aml_code,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision)
  },
  // PPTT Table
  {
    EFI_ACPI_6_4_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_STRUCTURE_SIGNATURE,
    EFI_ACPI_6_4_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdPptt),
    NULL,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision),
  },
  // SSDT Table - Cpu Topology
  {
    EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
    EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_REVISION,
    CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSsdtCpuTopology),
    NULL,
    0,
    FixedPcdGet64 (PcdAcpiDefaultOemRevision)
  },
};

/** The platform boot architecture information.
*/
STATIC
CM_ARM_BOOT_ARCH_INFO  BootArchInfo = {
  EFI_ACPI_6_4_ARM_PSCI_COMPLIANT
};

/** The platform power management profile information.
*/
STATIC
CM_ARM_POWER_MANAGEMENT_PROFILE_INFO  PmProfileInfo = {
  EFI_ACPI_6_4_PM_PROFILE_ENTERPRISE_SERVER
};

/** The platform generic timer information.
*/
STATIC
CM_ARM_GENERIC_TIMER_INFO  GenericTimerInfo = {
  SYSTEM_COUNTER_BASE_ADDRESS,
  SYSTEM_COUNTER_READ_BASE,
  FixedPcdGet32 (PcdArmArchTimerSecIntrNum),
  GTDT_GTIMER_FLAGS,
  FixedPcdGet32 (PcdArmArchTimerIntrNum),
  GTDT_GTIMER_FLAGS,
  FixedPcdGet32 (PcdArmArchTimerVirtIntrNum),
  GTDT_GTIMER_FLAGS,
  FixedPcdGet32 (PcdArmArchTimerHypIntrNum),
  GTDT_GTIMER_FLAGS,
  ARMARCH_TMR_HYPVIRT_PPI,
  GTDT_GTIMER_FLAGS
};

STATIC CM_ARM_GENERIC_WATCHDOG_INFO  Watchdog;

// SPCR Serial Port

/** The platform SPCR serial port information.
*/
STATIC
CM_ARM_SERIAL_PORT_INFO  SpcrSerialPort = {
  FixedPcdGet64 (PcdSbsaUartBaseTH500),                     // BaseAddress
  TH500_UART0_INTR,                                         // Interrupt
  FixedPcdGet64 (PcdUartDefaultBaudRate),                   // BaudRate
  FixedPcdGet32 (PL011UartClkInHz),                         // Clock
  EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_ARM_SBSA_GENERIC_UART   // Port subtype
};

/** Initialize the Serial Port entries in the platform configuration repository and patch DSDT.
 *
 * @param Repo Pointer to a repo structure that will be added to and updated with the data updated
 *
  @retval EFI_SUCCESS   Success
**/
STATIC
EFI_STATUS
EFIAPI
UpdateSerialPortInfo (
  EDKII_PLATFORM_REPOSITORY_INFO  **PlatformRepositoryInfo
  )
{
  EFI_STATUS                      Status;
  EDKII_PLATFORM_REPOSITORY_INFO  *Repo;
  UINT32                          Index;
  UINT8                           SerialPortConfig;
  CM_STD_OBJ_ACPI_TABLE_INFO      *NewAcpiTables;

  SerialPortConfig = PcdGet8 (PcdSerialPortConfig);
  if ((PcdGet8 (PcdSerialTypeConfig) != NVIDIA_SERIAL_PORT_TYPE_SBSA) ||
      (SerialPortConfig == NVIDIA_SERIAL_PORT_DISABLED))
  {
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < PcdGet32 (PcdConfigMgrObjMax); Index++) {
    if (NVIDIAPlatformRepositoryInfo[Index].CmObjectId == CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList)) {
      NewAcpiTables = (CM_STD_OBJ_ACPI_TABLE_INFO *)AllocateCopyPool (NVIDIAPlatformRepositoryInfo[Index].CmObjectSize + (sizeof (CM_STD_OBJ_ACPI_TABLE_INFO)), NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr);
      if (NewAcpiTables == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        return Status;
      }

      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr = NewAcpiTables;

      if (SerialPortConfig == NVIDIA_SERIAL_PORT_DBG2_SBSA) {
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_DEBUG_PORT_2_TABLE_SIGNATURE;
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_DEBUG_PORT_2_TABLE_REVISION;
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdDbg2);
      } else {
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE;
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION;
        NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSpcr);
      }

      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableData = NULL;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemTableId    = PcdGet64 (PcdAcpiTegraUartOemTableId);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemRevision   = FixedPcdGet64 (PcdAcpiDefaultOemRevision);
      NVIDIAPlatformRepositoryInfo[Index].CmObjectCount++;
      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize += sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);

      break;
    } else if (NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr == NULL) {
      break;
    }
  }

  Repo = *PlatformRepositoryInfo;

  if (SerialPortConfig == NVIDIA_SERIAL_PORT_DBG2_SBSA) {
    Repo->CmObjectId = CREATE_CM_ARM_OBJECT_ID (EArmObjSerialDebugPortInfo);
  } else {
    Repo->CmObjectId = CREATE_CM_ARM_OBJECT_ID (EArmObjSerialConsolePortInfo);
  }

  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (CM_ARM_SERIAL_PORT_INFO);
  Repo->CmObjectCount = 1;
  Repo->CmObjectPtr   = &SpcrSerialPort;
  Repo++;

  *PlatformRepositoryInfo = Repo;
  return EFI_SUCCESS;
}

/** Initialize the additional sockets info in the platform configuration repository and patch SSDT.
 *
 * @param Repo Pointer to a repo structure that will be added to and updated with the data updated
 *
  @retval EFI_SUCCESS   Success
**/
STATIC
EFI_STATUS
EFIAPI
UpdateAdditionalSocketInfo (
  EDKII_PLATFORM_REPOSITORY_INFO  **PlatformRepositoryInfo,
  UINTN                           SocketId
  )
{
  EFI_STATUS                  Status;
  UINT32                      Index;
  CM_STD_OBJ_ACPI_TABLE_INFO  *NewAcpiTables;

  for (Index = 0; Index < PcdGet32 (PcdConfigMgrObjMax); Index++) {
    if (NVIDIAPlatformRepositoryInfo[Index].CmObjectId == CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList)) {
      NewAcpiTables = (CM_STD_OBJ_ACPI_TABLE_INFO *)AllocateCopyPool (NVIDIAPlatformRepositoryInfo[Index].CmObjectSize + (sizeof (CM_STD_OBJ_ACPI_TABLE_INFO)), NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr);

      if (NewAcpiTables == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        return Status;
      }

      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr = NewAcpiTables;

      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_REVISION;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSsdt);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableData      = (EFI_ACPI_DESCRIPTION_HEADER *)AcpiTableArray[SocketId];
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemTableId         = PcdGet64 (PcdAcpiDefaultOemTableId);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemRevision        = FixedPcdGet64 (PcdAcpiDefaultOemRevision);
      NVIDIAPlatformRepositoryInfo[Index].CmObjectCount++;
      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize += sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);

      break;
    } else if (NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr == NULL) {
      break;
    }
  }

  return EFI_SUCCESS;
}

/** Initialize the socket info for tables needing bpmp in the platform configuration repository and patch SSDT.
 *
 * @param Repo Pointer to a repo structure that will be added to and updated with the data updated
 *
  @retval EFI_SUCCESS   Success
**/
STATIC
EFI_STATUS
EFIAPI
AddBpmpSocketInfo (
  EDKII_PLATFORM_REPOSITORY_INFO  **PlatformRepositoryInfo,
  UINTN                           SocketId
  )
{
  EFI_STATUS                  Status;
  UINT32                      Index;
  CM_STD_OBJ_ACPI_TABLE_INFO  *NewAcpiTables;

  for (Index = 0; Index < PcdGet32 (PcdConfigMgrObjMax); Index++) {
    if (NVIDIAPlatformRepositoryInfo[Index].CmObjectId == CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList)) {
      NewAcpiTables = (CM_STD_OBJ_ACPI_TABLE_INFO *)AllocateCopyPool (NVIDIAPlatformRepositoryInfo[Index].CmObjectSize + (sizeof (CM_STD_OBJ_ACPI_TABLE_INFO)), NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr);

      if (NewAcpiTables == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        return Status;
      }

      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr = NewAcpiTables;

      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_REVISION;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSsdt);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableData      = (EFI_ACPI_DESCRIPTION_HEADER *)AcpiBpmpTableArray[SocketId];
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemTableId         = PcdGet64 (PcdAcpiDefaultOemTableId);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemRevision        = FixedPcdGet64 (PcdAcpiDefaultOemRevision);
      NVIDIAPlatformRepositoryInfo[Index].CmObjectCount++;
      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize += sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);

      break;
    } else if (NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr == NULL) {
      break;
    }
  }

  return EFI_SUCCESS;
}

/** Initialize the ethernet controller entry in the platform configuration repository and patch SSDT.
 *
 * @param Repo Pointer to a repo structure that will be added to and updated with the data updated
 *
  @retval EFI_SUCCESS   Success
**/
STATIC
EFI_STATUS
EFIAPI
UpdateEthernetInfo (
  EDKII_PLATFORM_REPOSITORY_INFO  **PlatformRepositoryInfo
  )
{
  EFI_STATUS                  Status;
  UINT32                      Index;
  CM_STD_OBJ_ACPI_TABLE_INFO  *NewAcpiTables;
  TEGRA_PLATFORM_TYPE         PlatformType;

  PlatformType = TegraGetPlatform ();
  if (PlatformType != TEGRA_PLATFORM_VDK) {
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < PcdGet32 (PcdConfigMgrObjMax); Index++) {
    if (NVIDIAPlatformRepositoryInfo[Index].CmObjectId == CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList)) {
      NewAcpiTables = (CM_STD_OBJ_ACPI_TABLE_INFO *)AllocateCopyPool (NVIDIAPlatformRepositoryInfo[Index].CmObjectSize + (sizeof (CM_STD_OBJ_ACPI_TABLE_INFO)), NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr);

      if (NewAcpiTables == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        return Status;
      }

      NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr = NewAcpiTables;

      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableSignature = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableRevision  = EFI_ACPI_6_4_SECONDARY_SYSTEM_DESCRIPTION_TABLE_REVISION;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].TableGeneratorId   = CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSsdt);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].AcpiTableData      = (EFI_ACPI_DESCRIPTION_HEADER *)ssdteth_aml_code;
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemTableId         = PcdGet64 (PcdAcpiDefaultOemTableId);
      NewAcpiTables[NVIDIAPlatformRepositoryInfo[Index].CmObjectCount].OemRevision        = FixedPcdGet64 (PcdAcpiDefaultOemRevision);
      NVIDIAPlatformRepositoryInfo[Index].CmObjectCount++;
      NVIDIAPlatformRepositoryInfo[Index].CmObjectSize += sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);

      break;
    } else if (NVIDIAPlatformRepositoryInfo[Index].CmObjectPtr == NULL) {
      break;
    }
  }

  return EFI_SUCCESS;
}

/** patch GED data in DSDT.

  @retval EFI_SUCCESS   Success

**/
STATIC
EFI_STATUS
EFIAPI
UpdateGedInfo (
  )
{
  EFI_STATUS                  Status;
  NVIDIA_AML_NODE_INFO        AcpiNodeInfo;
  RAS_PCIE_DPC_COMM_BUF_INFO  *DpcCommBuf = NULL;

  Status = gBS->LocateProtocol (
                  &gNVIDIARasNsCommPcieDpcDataProtocolGuid,
                  NULL,
                  (VOID **)&DpcCommBuf
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      EFI_D_ERROR,
      "%a: Couldn't get gNVIDIARasNsCommPcieDpcDataProtocolGuid protocol: %r\n",
      __FUNCTION__,
      Status
      ));
  }

  if (DpcCommBuf == NULL) {
    // Protocol installed NULL interface. Skip using it.
    return EFI_SUCCESS;
  }

  Status = PatchProtocol->FindNode (PatchProtocol, ACPI_GED1_SMR1, &AcpiNodeInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: GED node is not found for patching %a - %r\r\n", __FUNCTION__, ACPI_GED1_SMR1, Status));
    return EFI_SUCCESS;
  }

  Status = PatchProtocol->SetNodeData (PatchProtocol, &AcpiNodeInfo, &DpcCommBuf->PcieBase, 8);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error updating %a - %r\r\n", __FUNCTION__, ACPI_GED1_SMR1, Status));
  }

  return Status;
}

/** patch QSPI1 data in DSDT.

  @retval EFI_SUCCESS   Success

**/
STATIC
EFI_STATUS
EFIAPI
UpdateQspiInfo (
  VOID
  )
{
  EFI_STATUS            Status;
  UINT32                NumberOfQspiControllers;
  UINT32                *QspiHandles;
  UINT32                Index;
  VOID                  *Dtb;
  INT32                 NodeOffset;
  NVIDIA_AML_NODE_INFO  AcpiNodeInfo;
  UINT8                 QspiStatus;

  NumberOfQspiControllers = 0;
  Status                  = GetMatchingEnabledDeviceTreeNodes ("nvidia,tegra186-qspi", NULL, &NumberOfQspiControllers);
  if (Status == EFI_NOT_FOUND) {
    return EFI_SUCCESS;
  } else if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  QspiHandles = NULL;
  QspiHandles = (UINT32 *)AllocatePool (sizeof (UINT32) * NumberOfQspiControllers);
  if (QspiHandles == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = GetMatchingEnabledDeviceTreeNodes ("nvidia,tegra186-qspi", QspiHandles, &NumberOfQspiControllers);
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  for (Index = 0; Index < NumberOfQspiControllers; Index++) {
    Status = GetDeviceTreeNode (QspiHandles[Index], &Dtb, &NodeOffset);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to get device node info - %r\r\n", __FUNCTION__, Status));
      goto ErrorExit;
    }

    if (NULL == fdt_getprop (Dtb, NodeOffset, "nvidia,secure-qspi-controller", NULL)) {
      Status = PatchProtocol->FindNode (PatchProtocol, ACPI_QSPI1_STA, &AcpiNodeInfo);
      if (EFI_ERROR (Status)) {
        goto ErrorExit;
      }

      if (AcpiNodeInfo.Size > sizeof (QspiStatus)) {
        Status = EFI_DEVICE_ERROR;
        goto ErrorExit;
      }

      QspiStatus = 0xF;
      Status     = PatchProtocol->SetNodeData (PatchProtocol, &AcpiNodeInfo, &QspiStatus, sizeof (QspiStatus));
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "%a: Error updating %a - %r\r\n", __FUNCTION__, ACPI_QSPI1_STA, Status));
      }
    }
  }

ErrorExit:
  if (QspiHandles != NULL) {
    FreePool (QspiHandles);
  }

  return Status;
}

/** patch I2C3 and SSIF data in DSDT.

  @retval EFI_SUCCESS   Success

**/
STATIC
EFI_STATUS
EFIAPI
UpdateSSIFInfo (
  VOID
  )
{
  EFI_STATUS            Status;
  UINT32                NumberOfI2CControllers;
  UINT32                *I2CHandles;
  UINT32                Index;
  VOID                  *Dtb;
  INT32                 NodeOffset;
  INT32                 SubNodeOffset;
  NVIDIA_AML_NODE_INFO  AcpiNodeInfo;
  UINT8                 I2CStatus;
  UINT8                 SSIFStatus;

  NumberOfI2CControllers = 0;
  Status                 = GetMatchingEnabledDeviceTreeNodes ("nvidia,tegra234-i2c", NULL, &NumberOfI2CControllers);
  if (Status == EFI_NOT_FOUND) {
    return EFI_SUCCESS;
  } else if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  I2CHandles = NULL;
  I2CHandles = (UINT32 *)AllocatePool (sizeof (UINT32) * NumberOfI2CControllers);
  if (I2CHandles == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = GetMatchingEnabledDeviceTreeNodes ("nvidia,tegra234-i2c", I2CHandles, &NumberOfI2CControllers);
  if (EFI_ERROR (Status)) {
    goto ErrorExit;
  }

  for (Index = 0; Index < NumberOfI2CControllers; Index++) {
    Status = GetDeviceTreeNode (I2CHandles[Index], &Dtb, &NodeOffset);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to get device node info - %r\r\n", __FUNCTION__, Status));
      goto ErrorExit;
    }

    SubNodeOffset = fdt_subnode_offset (Dtb, NodeOffset, "bmc-ssif");
    if (SubNodeOffset != 0) {
      /* Update I2C3 Status */
      Status = PatchProtocol->FindNode (PatchProtocol, ACPI_I2C3_STA, &AcpiNodeInfo);
      if (EFI_ERROR (Status)) {
        goto ErrorExit;
      }

      if (AcpiNodeInfo.Size > sizeof (I2CStatus)) {
        Status = EFI_DEVICE_ERROR;
        goto ErrorExit;
      }

      I2CStatus = 0xF;
      Status    = PatchProtocol->SetNodeData (PatchProtocol, &AcpiNodeInfo, &I2CStatus, sizeof (I2CStatus));
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "%a: Error updating %a - %r\r\n", __FUNCTION__, ACPI_I2C3_STA, Status));
        goto ErrorExit;
      }

      /* Update SSIF Status */
      Status = PatchProtocol->FindNode (PatchProtocol, ACPI_SSIF_STA, &AcpiNodeInfo);
      if (EFI_ERROR (Status)) {
        goto ErrorExit;
      }

      if (AcpiNodeInfo.Size > sizeof (SSIFStatus)) {
        Status = EFI_DEVICE_ERROR;
        goto ErrorExit;
      }

      SSIFStatus = 0xF;
      Status     = PatchProtocol->SetNodeData (PatchProtocol, &AcpiNodeInfo, &I2CStatus, sizeof (SSIFStatus));
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "%a: Error updating %a - %r\r\n", __FUNCTION__, ACPI_SSIF_STA, Status));
      }
    }
  }

ErrorExit:
  if (I2CHandles != NULL) {
    FreePool (I2CHandles);
  }

  return Status;
}

/** Initialize the platform configuration repository.
  @retval EFI_SUCCESS   Success
**/
STATIC
EFI_STATUS
EFIAPI
InitializePlatformRepository (
  VOID
  )
{
  UINTN                           Index;
  EFI_STATUS                      Status;
  EDKII_PLATFORM_REPOSITORY_INFO  *Repo;
  EDKII_PLATFORM_REPOSITORY_INFO  *RepoEnd;
  VOID                            *DtbBase;
  UINTN                           DtbSize;
  INTN                            NodeOffset;
  BOOLEAN                         SkipSlit;
  BOOLEAN                         SkipSrat;
  BOOLEAN                         SkipHmat;
  BOOLEAN                         SkipIort;
  BOOLEAN                         SkipMpam;
  BOOLEAN                         SkipApmt;
  UINTN                           SocketId;
  TEGRA_PLATFORM_TYPE             PlatformType;

  NVIDIAPlatformRepositoryInfo = (EDKII_PLATFORM_REPOSITORY_INFO *)AllocateZeroPool (sizeof (EDKII_PLATFORM_REPOSITORY_INFO) * PcdGet32 (PcdConfigMgrObjMax));

  Repo    = NVIDIAPlatformRepositoryInfo;
  RepoEnd = Repo + PcdGet32 (PcdConfigMgrObjMax);

  Repo->CmObjectId    = CREATE_CM_STD_OBJECT_ID (EStdObjCfgMgrInfo);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (CmInfo);
  Repo->CmObjectCount = sizeof (CmInfo) / sizeof (CM_STD_OBJ_CONFIGURATION_MANAGER_INFO);
  Repo->CmObjectPtr   = &CmInfo;
  Repo++;

  Repo->CmObjectId    = CREATE_CM_STD_OBJECT_ID (EStdObjAcpiTableList);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (CmAcpiTableList);
  Repo->CmObjectCount = sizeof (CmAcpiTableList) / sizeof (CM_STD_OBJ_ACPI_TABLE_INFO);
  Repo->CmObjectPtr   = &CmAcpiTableList;

  for (Index = 0; Index < Repo->CmObjectCount; Index++) {
    if ((CmAcpiTableList[Index].AcpiTableSignature != EFI_ACPI_6_4_DEBUG_PORT_2_TABLE_SIGNATURE) &&
        (CmAcpiTableList[Index].AcpiTableSignature != EFI_ACPI_6_4_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE))
    {
      CmAcpiTableList[Index].OemTableId =  PcdGet64 (PcdAcpiDefaultOemTableId);
    }
  }

  Repo++;

  Repo->CmObjectId    = CREATE_CM_ARM_OBJECT_ID (EArmObjBootArchInfo);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (BootArchInfo);
  Repo->CmObjectCount = sizeof (BootArchInfo) / sizeof (CM_ARM_BOOT_ARCH_INFO);
  Repo->CmObjectPtr   = &BootArchInfo;
  Repo++;

  Repo->CmObjectId    = CREATE_CM_ARM_OBJECT_ID (EArmObjPowerManagementProfileInfo);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (PmProfileInfo);
  Repo->CmObjectCount = sizeof (PmProfileInfo) / sizeof (CM_ARM_POWER_MANAGEMENT_PROFILE_INFO);
  Repo->CmObjectPtr   = &PmProfileInfo;
  Repo++;

  Status = UpdateGicInfo (&Repo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Repo->CmObjectId    = CREATE_CM_ARM_OBJECT_ID (EArmObjGenericTimerInfo);
  Repo->CmObjectToken = CM_NULL_TOKEN;
  Repo->CmObjectSize  = sizeof (GenericTimerInfo);
  Repo->CmObjectCount = sizeof (GenericTimerInfo) / sizeof (CM_ARM_GENERIC_TIMER_INFO);
  Repo->CmObjectPtr   = &GenericTimerInfo;
  Repo++;

  if (TegraGetPlatform () != TEGRA_PLATFORM_VDK) {
    Watchdog.ControlFrameAddress = PcdGet64 (PcdGenericWatchdogControlBase);
    Watchdog.RefreshFrameAddress = PcdGet64 (PcdGenericWatchdogRefreshBase);
    Watchdog.TimerGSIV           = PcdGet32 (PcdGenericWatchdogEl2IntrNum);
    Watchdog.Flags               = SBSA_WATCHDOG_FLAGS;
    Repo->CmObjectId             = CREATE_CM_ARM_OBJECT_ID (EArmObjPlatformGenericWatchdogInfo);
    Repo->CmObjectToken          = CM_NULL_TOKEN;
    Repo->CmObjectSize           = sizeof (Watchdog);
    Repo->CmObjectCount          = sizeof (Watchdog) / sizeof (CM_ARM_GENERIC_WATCHDOG_INFO);
    Repo->CmObjectPtr            = &Watchdog;
    Repo++;
  }

  SkipSlit = FALSE;
  SkipSrat = FALSE;
  SkipHmat = FALSE;
  SkipIort = FALSE;
  SkipMpam = FALSE;
  SkipApmt = FALSE;
  Status   = DtPlatformLoadDtb (&DtbBase, &DtbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NodeOffset = fdt_path_offset (DtbBase, "/firmware/uefi");
  if (NodeOffset >= 0) {
    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-slit-table", NULL)) {
      SkipSlit = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip SLIT Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-srat-table", NULL)) {
      SkipSrat = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip SRAT Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-hmat-table", NULL)) {
      SkipHmat = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip HMAT Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-iort-table", NULL)) {
      SkipIort = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip IORT Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-mpam-table", NULL)) {
      SkipMpam = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip MPAM Table\r\n", __FUNCTION__));
    }

    if (NULL != fdt_get_property (DtbBase, NodeOffset, "skip-apmt-table", NULL)) {
      SkipApmt = TRUE;
      DEBUG ((DEBUG_ERROR, "%a: Skip APMT Table\r\n", __FUNCTION__));
    }
  }

  Status = UpdateCpuInfo (&Repo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateSerialPortInfo (&Repo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateEthernetInfo (&Repo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateGedInfo ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // SSDT for socket1 onwards
  for (SocketId = 1; SocketId < PcdGet32 (PcdTegraMaxSockets); SocketId++) {
    if (!IsSocketEnabled (SocketId)) {
      continue;
    }

    Status = UpdateAdditionalSocketInfo (&Repo, SocketId);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  // BPMP SSDT
  PlatformType = TegraGetPlatform ();
  if (PlatformType == TEGRA_PLATFORM_SILICON) {
    for (SocketId = 0; SocketId < PcdGet32 (PcdTegraMaxSockets); SocketId++) {
      if (!IsSocketEnabled (SocketId)) {
        continue;
      }

      Status = AddBpmpSocketInfo (&Repo, SocketId);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  Status = RegisterProtocolBasedObjects (NVIDIAPlatformRepositoryInfo, &Repo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!SkipIort) {
    Status = InstallIoRemappingTable (&Repo, (UINTN)RepoEnd, NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if ((!SkipMpam) && (IsMpamEnabled ())) {
    Status = InstallMpamTable (&Repo, (UINTN)RepoEnd, NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = GenerateHbmMemPxmDmnMap ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!SkipSrat) {
    Status = InstallStaticResourceAffinityTable (&Repo, (UINTN)RepoEnd, NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (!SkipSlit) {
    Status = InstallStaticLocalityInformationTable (&Repo, (UINTN)RepoEnd, NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (!SkipHmat) {
    Status = InstallHeterogeneousMemoryAttributeTable (&Repo, (UINTN)RepoEnd, NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (!SkipApmt) {
    Status = InstallArmPerformanceMonitoringUnitTable (NVIDIAPlatformRepositoryInfo);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = InstallCmSmbiosTableList (&Repo, (UINTN)RepoEnd);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ASSERT ((UINTN)Repo <= (UINTN)RepoEnd);

  return EFI_SUCCESS;
}

/**
  Entrypoint of Configuration Manager Data Dxe.

  @param  ImageHandle
  @param  SystemTable

  @return EFI_SUCCESS
  @return EFI_LOAD_ERROR
  @return EFI_OUT_OF_RESOURCES

**/
EFI_STATUS
EFIAPI
ConfigurationManagerDataDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN       ChipID;
  EFI_STATUS  Status;

  ChipID = TegraGetChipID ();
  if (ChipID != TH500_CHIP_ID) {
    return EFI_SUCCESS;
  }

  Status = gBS->LocateProtocol (&gNVIDIAAmlPatchProtocolGuid, NULL, (VOID **)&PatchProtocol);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PatchProtocol->RegisterAmlTables (
                            PatchProtocol,
                            AcpiTableArray,
                            OffsetTableArray,
                            ARRAY_SIZE (AcpiTableArray)
                            );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = InitializeIoRemappingNodes ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = InitializePlatformRepository ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateQspiInfo ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateSSIFInfo ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return gBS->InstallMultipleProtocolInterfaces (
                &ImageHandle,
                &gNVIDIAConfigurationManagerDataProtocolGuid,
                (VOID *)NVIDIAPlatformRepositoryInfo,
                NULL
                );
}
