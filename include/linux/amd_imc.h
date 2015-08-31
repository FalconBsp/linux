#ifndef _AMD_IMC_H_
#define _AMD_IMC_H_

/* Module and version information */
#define IMC_VERSION "0.1"
#define IMC_MODULE_NAME "AMD IMC"
#define IMC_DRIVER_NAME   IMC_MODULE_NAME ", v" IMC_VERSION

#define DRV_NAME "amd_imc"

/* IO port address for indirect access using the ACPI PM registers */
#define AMD_IO_PM_INDEX_REG		0xCD6
#define AMD_IO_PM_DATA_REG		0xCD7

#define AMD_GPIO_ACPIMMIO_BASE		0xFED80000
#define AMD_PM_ACPI_MMIO_BASE0		0x24
#define AMD_PM_ACPI_MMIO_BASE1		0x25
#define AMD_PM_ACPI_MMIO_BASE2		0x26
#define AMD_PM_ACPI_MMIO_BASE3		0x27

#define AMD_ACPI_MMIO_ADDR_MASK		~0x1FFF

/* Offset of IMC Strap Status register in the ACPI MMIO region */
#define AMD_IMC_STRAP_STATUS_OFFSET	0xE80
 #define AMD_IMC_ENABLED		0x4
#define AMD_IMC_STRAP_STATUS_SIZE	4

#define PCI_DEVICE_ID_AMD_LPC_BRIDGE	0x790E
 #define AMD_PCI_IMC_PORT_ADDR_REG	0xA4
  #define AMD_IMC_PORT_ACTIVE		0x0001

/* Device configuration state fields */
#define AMD_DEVICE_ENTER_CONFIG_STATE	0x5A
#define AMD_DEVICE_EXIT_CONFIG_STATE	0xA5

/* Global configuration registers */
#define AMD_SET_LOGICAL_DEVICE		0x07
 #define AMD_SET_DEVICE_9		0x09
#define AMD_MSG_REG_HIGH		0x60
#define AMD_MSG_REG_LOW			0x61

/* IMC index and data port offsets for indirect access */
#define AMD_IMC_INDEX_REG_OFFSET	0x00
#define AMD_IMC_DATA_REG_OFFSET		0x01

/* Message register index and data port offsets for indirect access */
#define AMD_MSG_INDEX_REG_OFFSET	0x00
#define AMD_MSG_DATA_REG_OFFSET		0x01

/* IMC message registers */
#define AMD_MSG_SYS_TO_IMC		0x80
 #define AMD_IMC_ROM_OWNERSHIP_SEM	0x96
#define AMD_MSG_REG0			0x82
 #define AMD_IMC_FUNC_NOT_SUPP		0x00
 #define AMD_IMC_FUNC_COMPLETED		0xFA
#define AMD_MSG_REG1			0x83
 #define AMD_IMC_ENTER_SCRATCH_RAM	0xB4
 #define AMD_IMC_EXIT_SCRATCH_RAM	0xB5

/* Extern functions */
#ifdef CONFIG_AMD_IMC
extern void amd_imc_enter_scratch_ram(void);
extern void amd_imc_exit_scratch_ram(void);
#else
void amd_imc_enter_scratch_ram(void) {}
void amd_imc_exit_scratch_ram(void) {}
#endif

#endif /* _AMD_IMC_H_ */
