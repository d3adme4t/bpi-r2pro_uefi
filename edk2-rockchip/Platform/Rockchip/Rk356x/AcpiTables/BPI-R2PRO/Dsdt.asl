/** @file
*  Differentiated System Description Table Fields (DSDT) for the Firefly ROC-RK3566-PC.
*
*  Copyright (c) 2022, Jared McNeill <jmcneill@invisible.ca>
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/Acpi60.h>

DefinitionBlock ("DsdtTable.aml", "DSDT",
                 EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_REVISION,
                 "RKCP  ", "RK356X  ", FixedPcdGet32 (PcdAcpiDefaultOemRevision)) {
  Scope (_SB) {

    include ("Cpu.asl")
    include ("Tsadc.asl") # thermal sensors
    include ("Uart.asl")
    include ("Usb3.asl")
#    include ("Mshc.asl") # SDIO
#    include ("Emmc.asl") # eMMC

  } // Scope (_SB)
}
