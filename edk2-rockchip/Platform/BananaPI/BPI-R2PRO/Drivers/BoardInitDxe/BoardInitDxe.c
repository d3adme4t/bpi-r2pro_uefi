/** @file
 *
 *  Board init for the ROC-RK3568-PC platform
 *
 *  Copyright (c) 2021-2022, Jared McNeill <jmcneill@invisible.ca>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Base.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/CruLib.h>
#include <Library/GpioLib.h>
#include <Library/I2cLib.h>
#include <Library/MultiPhyLib.h>
#include <Library/OtpLib.h>
#include <Library/SocLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>
#include <Protocol/ArmScmi.h>
#include <Protocol/ArmScmiClockProtocol.h>

#include <IndustryStandard/Rk356x.h>
#include <IndustryStandard/Rk356xCru.h>

//#include "EthernetPhy.h"

/*
 * GMAC registers
 */
#define GMAC0_MAC_ADDRESS0_LOW  (GMAC0_BASE + 0x0304)
#define GMAC0_MAC_ADDRESS0_HIGH (GMAC0_BASE + 0x0300)
#define GMAC1_MAC_ADDRESS0_LOW  (GMAC1_BASE + 0x0304)
#define GMAC1_MAC_ADDRESS0_HIGH (GMAC1_BASE + 0x0300)

#define GRF_MAC0_CON0           (SYS_GRF + 0x0380)
#define GRF_MAC1_CON0           (SYS_GRF + 0x0388)
#define  CLK_RX_DL_CFG_SHIFT    8
#define  CLK_TX_DL_CFG_SHIFT    0
#define GRF_MAC0_CON1           (SYS_GRF + 0x0384)
#define GRF_MAC1_CON1           (SYS_GRF + 0x038C)
#define  PHY_INTF_SEL_SHIFT     4
#define  PHY_INTF_SEL_MASK      (0x7U << PHY_INTF_SEL_SHIFT)
#define  PHY_INTF_SEL_RGMII     (1U << PHY_INTF_SEL_SHIFT)
#define  FLOWCTRL               BIT3
#define  MAC_SPEED              BIT2
#define  RXCLK_DLY_ENA          BIT1
#define  TXCLK_DLY_ENA          BIT0
#define GRF_IOFUNC_SEL0         (SYS_GRF + 0x0300)
#define  GMAC1_IOMUX_SEL        BIT8

#define TX_DELAY_GMAC0          0x3C
#define RX_DELAY_GMAC0          0x2F
#define TX_DELAY_GMAC1          0x4F
#define RX_DELAY_GMAC1          0x26

/*
 * PMIC registers
*/
#define PMIC_I2C_ADDR           0x20

#define PMIC_CHIP_NAME          0xed
#define PMIC_CHIP_VER           0xee
#define PMIC_POWER_EN1          0xb2
#define PMIC_POWER_EN2          0xb3
#define PMIC_POWER_EN3          0xb4
#define PMIC_LDO1_ON_VSEL       0xcc
#define PMIC_LDO9_ON_VSEL       0xdc

/*
 * CPU_GRF registers
*/
#define GRF_CPU_COREPVTPLL_CON0               (CPU_GRF + 0x0010)
#define  CORE_PVTPLL_RING_LENGTH_SEL_SHIFT    3
#define  CORE_PVTPLL_RING_LENGTH_SEL_MASK     (0x1FU << CORE_PVTPLL_RING_LENGTH_SEL_SHIFT)
#define  CORE_PVTPLL_OSC_EN                   BIT1
#define  CORE_PVTPLL_START                    BIT0

/*
 * PMU registers
 */
#define PMU_NOC_AUTO_CON0                     (PMU_BASE + 0x0070)
#define PMU_NOC_AUTO_CON1                     (PMU_BASE + 0x0074)

/*STATIC CONST GPIO_IOMUX_CONFIG mSdmmc2IomuxConfig[] = {
  { "sdmmc0_d0m0",        1, GPIO_PIN_PD5, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
  { "sdmmc0_d1m0",        1, GPIO_PIN_PD6, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
  { "sdmmc0_d2m0",        1, GPIO_PIN_PD7, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
  { "sdmmc0_d3m0",        2, GPIO_PIN_PA0, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
  { "sdmmc0_cmdm0",       2, GPIO_PIN_PA1, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
  { "sdmmc0_clkm0",       2, GPIO_PIN_PA1, 3, GPIO_PIN_PULL_UP,   GPIO_PIN_DRIVE_2 },
};*/

STATIC
EFI_STATUS
BoardInitSetCpuSpeed (
  VOID
  )
{
  EFI_STATUS             Status;
  SCMI_CLOCK_PROTOCOL    *ClockProtocol;
  EFI_GUID               ClockProtocolGuid = ARM_SCMI_CLOCK_PROTOCOL_GUID;
  UINT64                 CpuRate;
  UINT32                 ClockId;
  UINT32                 ClockProtocolVersion;
  BOOLEAN                Enabled;
  CHAR8                  ClockName[SCMI_MAX_STR_LEN];
  UINT32                 TotalRates = 0;
  UINT32                 ClockRateSize;
  SCMI_CLOCK_RATE        *ClockRate;
  SCMI_CLOCK_RATE_FORMAT ClockRateFormat;

  Status = gBS->LocateProtocol (
                  &ClockProtocolGuid,
                  NULL,
                  (VOID**)&ClockProtocol
                  );
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = ClockProtocol->GetVersion (ClockProtocol, &ClockProtocolVersion);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }
  DEBUG ((DEBUG_ERROR, "SCMI clock management protocol version = %x\n",
    ClockProtocolVersion));

  ClockId = 0;

  Status = ClockProtocol->GetClockAttributes (
                            ClockProtocol,
                            ClockId,
                            &Enabled,
                            ClockName
                            );
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = ClockProtocol->RateGet (ClockProtocol, ClockId, &CpuRate);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SCMI: %a: Current rate is %uHz\n", ClockName, CpuRate));

  TotalRates = 0;
  ClockRateSize = 0;
  Status = ClockProtocol->DescribeRates (
                            ClockProtocol,
                            ClockId,
                            &ClockRateFormat,
                            &TotalRates,
                            &ClockRateSize,
                            ClockRate
                            );
  if (EFI_ERROR (Status) && Status != EFI_BUFFER_TOO_SMALL) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }
  ASSERT (Status == EFI_BUFFER_TOO_SMALL);
  ASSERT (TotalRates > 0);
  ASSERT (ClockRateFormat == ScmiClockRateFormatDiscrete);
  if (Status != EFI_BUFFER_TOO_SMALL ||
      TotalRates == 0 ||
      ClockRateFormat != ScmiClockRateFormatDiscrete) {
    return EFI_DEVICE_ERROR;
  }
  
  ClockRateSize = sizeof (*ClockRate) * TotalRates;
  ClockRate = AllocatePool (ClockRateSize);
  ASSERT (ClockRate != NULL);
  if (ClockRate == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = ClockProtocol->DescribeRates (
                            ClockProtocol,
                            ClockId,
                            &ClockRateFormat,
                            &TotalRates,
                            &ClockRateSize,
                            ClockRate
                            );
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    FreePool (ClockRate);
    return Status;
  }

  CpuRate = ClockRate[TotalRates - 1].DiscreteRate.Rate;
  FreePool (ClockRate);

  DEBUG ((DEBUG_INFO, "SCMI: %a: New rate is %uHz\n", ClockName, CpuRate));

  Status = ClockProtocol->RateSet (
                            ClockProtocol,
                            ClockId,
                            CpuRate
                            );
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = ClockProtocol->RateGet (ClockProtocol, ClockId, &CpuRate);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SCMI: %a: Current rate is %uHz\n", ClockName, CpuRate));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PmicRead (
  IN UINT8 Register,
  OUT UINT8 *Value
  )
{
  return I2cRead (I2C0_BASE, PMIC_I2C_ADDR,
                  &Register, sizeof (Register),
                  Value, sizeof (*Value));
}

STATIC
EFI_STATUS
PmicWrite (
  IN UINT8 Register,
  IN UINT8 Value
  )
{
  return I2cWrite (I2C0_BASE, PMIC_I2C_ADDR,
                  &Register, sizeof (Register),
                  &Value, sizeof (Value));
}

STATIC
VOID
BoardInitPmic (
  VOID
  )
{
  EFI_STATUS Status;
  UINT16 ChipName;
  UINT8 ChipVer;
  UINT8 Value;

  DEBUG ((DEBUG_INFO, "BOARD: PMIC init\n"));

  GpioPinSetPull (0, GPIO_PIN_PB1, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (0, GPIO_PIN_PB1, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (0, GPIO_PIN_PB1, 1);			// Set GPIO0_B1 to I2C0_SCL
  GpioPinSetPull (0, GPIO_PIN_PB2, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (0, GPIO_PIN_PB2, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (0, GPIO_PIN_PB2, 1);			// Set GPIO0_B2 to I2C0_SDA

  Status = PmicRead (PMIC_CHIP_NAME, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Failed to read PMIC chip name! %r\n", Status));
    ASSERT (FALSE);
  }
  ChipName = (UINT16)Value << 4;

  Status = PmicRead (PMIC_CHIP_VER, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Failed to read PMIC chip version! %r\n", Status));
    ASSERT (FALSE);
  }
  ChipName |= (Value >> 4) & 0xF;
  ChipVer = Value & 0xF;

  DEBUG ((DEBUG_INFO, "PMIC: Detected RK%03X ver 0x%X\n", ChipName, ChipVer));
  ASSERT (ChipName == 0x809);

  // Set LDO1 and LDO9 voltage
  PmicWrite (PMIC_LDO1_ON_VSEL, 0x0c);
  PmicWrite (PMIC_LDO9_ON_VSEL, 0x30);

  /* Check LD01 and LD09 are configured correctly. */
  PmicRead (PMIC_LDO1_ON_VSEL, &Value);
  ASSERT (Value == 0x0c); /* 0.9V */
  PmicRead (PMIC_LDO9_ON_VSEL, &Value);
  ASSERT (Value == 0x30); /* 1.8V */

  /* Enable LDO1 and LDO9 for HDMI */
  PmicWrite (PMIC_POWER_EN1, 0x11);
  PmicWrite (PMIC_POWER_EN3, 0x11);
}

EFI_STATUS
EFIAPI
BoardInitDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "BOARD: BoardInitDriverEntryPoint() called\n"));

////////rk3568-bpi-r2-pro.dts
//&pmu_io_domains {
//        pmuio1-supply = <&vcc3v3_pmu>;
//        pmuio2-supply = <&vcc3v3_pmu>;
//        vccio1-supply = <&vccio_acodec>;//3v3
//        vccio3-supply = <&vccio_sd>;
//        vccio4-supply = <&vcc_3v3>;
//        vccio5-supply = <&vcc_3v3>;
//        vccio6-supply = <&vcc_1v8>;
//        vccio7-supply = <&vcc_3v3>;
//        status = "okay";
//};

//  SocSetDomainVoltage (PMUIO1, VCC_3V3);
  SocSetDomainVoltage (PMUIO2, VCC_3V3);
//  SocSetDomainVoltage (VCCIO1, VCC_3V3); //I2S1
//  SocSetDomainVoltage (VCCIO3, VCC_3V3); //SD
  SocSetDomainVoltage (VCCIO4, VCC_3V3); // GMAC0 (MT7531)
  SocSetDomainVoltage (VCCIO5, VCC_3V3); // PCIE3;i2c5;i2s3
  SocSetDomainVoltage (VCCIO6, VCC_1V8); // GMAG1 (WAN)
  SocSetDomainVoltage (VCCIO7, VCC_3V3); // HDMI; SPI3

  BoardInitPmic ();

  /* I2C3 bus, used for RTC */
  GpioPinSetPull (1, GPIO_PIN_PA1, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (1, GPIO_PIN_PA1, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (1, GPIO_PIN_PA1, 1);
  GpioPinSetPull (1, GPIO_PIN_PA2, GPIO_PIN_PULL_NONE);
  GpioPinSetInput (1, GPIO_PIN_PA2, GPIO_PIN_INPUT_SCHMITT);
  GpioPinSetFunction (1, GPIO_PIN_PA2, 1);

  /* Update CPU speed */
  BoardInitSetCpuSpeed ();

  /* Enable automatic clock gating */
  MmioWrite32 (PMU_NOC_AUTO_CON0, 0xFFFFFFFFU);
  MmioWrite32 (PMU_NOC_AUTO_CON1, 0x000F000FU);

  /* Set core_pvtpll ring length */
  MmioWrite32 (GRF_CPU_COREPVTPLL_CON0,
               ((CORE_PVTPLL_RING_LENGTH_SEL_MASK | CORE_PVTPLL_OSC_EN | CORE_PVTPLL_START) << 16) |
               (5U << CORE_PVTPLL_RING_LENGTH_SEL_SHIFT) | CORE_PVTPLL_OSC_EN | CORE_PVTPLL_START);

  /* Configure MULTI-PHY 0 and 1 for USB3 mode */
  MultiPhySetMode (0, MULTIPHY_MODE_USB3);
  MultiPhySetMode (1, MULTIPHY_MODE_USB3);
  /* Configure MULTI-PHY 2 for SATA mode */
  MultiPhySetMode (2, MULTIPHY_MODE_SATA);


  /* Set GPIO0 PA6 (USB_HOST5V_EN) output high to power USB ports */
  GpioPinSetDirection (0, GPIO_PIN_PA6, GPIO_PIN_OUTPUT);
  GpioPinWrite (0, GPIO_PIN_PA6, TRUE);

  return EFI_SUCCESS;
}
