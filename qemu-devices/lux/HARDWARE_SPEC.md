# LuxPCIe Gen 1: Multi-Function "System on Chip"

## Overview

The LuxPCIe Gen 1 is a virtual PCIe "System on Chip" (SoC) designed to demonstrate advanced Linux kernel hardware interactions, including Multifunction PCIe, MSI-X, Generic IRQ domains, and Memory-Mapped I/O.

### PCI Identity

- **Vendor ID:** `0x1234` (Experimental)
- **Class Code:** `0x0880` (Generic System Peripheral)

The Silicon die is split into two physical PCI Functions:

- **Function 0 (`0x0001`):** The "Data Plane" (GPIO and Smart LEDs)
- **Function 1 (`0x0002`):** The "Management Engine" (Hardware Timer and Programmable Interrupt Controller)

---

## Function 0: GPIO & Smart LEDs (Device ID: 0x0001)

### F0 BAR 0: Generic GPIO Interface

- **Size:** 4096 Bytes (4KB)
- **Semantic:** Provides a 1-bit binary interface for 64 LED pins.

| Offset | Name        | Access | Description                                               |
| :----- | :---------- | :----- | :-------------------------------------------------------- |
| `0x00` | `REG_MAGIC` | RO     | Magic: `0x4750494F` ("GPIO"). 4 Bytes.                    |
| `0x04` | `REG_VER`   | RO     | Version: `0x00000001`. 4 Bytes.                           |
| `0x08` | `REG_DIR`   | RW     | Direction (1=Out, 0=In). 8 Bytes.                         |
| `0x10` | `REG_DATA`  | RW     | Data. Read/Write current pin states. 8 Bytes.             |
| `0x18` | `REG_SET`   | WO     | Atomic Set. Write `1` to a bit to set pin HIGH. 8 Bytes.  |
| `0x20` | `REG_CLR`   | WO     | Atomic Clear. Write `1` to a bit to set pin LOW. 8 Bytes. |

### F0 BAR 1: Smart LED Controller Interface

- **Size:** 4096 Bytes (4KB)
- **Semantic:** An array of 64 structures representing 32-bit ARGB LEDs.

| Offset Range | Name         | Access | Description                     |
| :----------- | :----------- | :----- | :------------------------------ |
| `i*8 + 0x0`  | `LED[i].ST`  | RW     | State of LED `i`.               |
| `i*8 + 0x4`  | `LED[i].COL` | RW     | Color of LED `i`. (ARGB layout) |

---

## Function 1: Management Engine (Device ID: 0x0002)

### F1 BAR 0: IRQ Controller & Hardware Timer

- **Size:** 4096 Bytes (4KB)
- **Semantic:** A Programmable Interrupt Controller (PIC) and 64-bit Hardware Clock.

| Offset | Name             | Access | Description                                                                      |
| :----- | :--------------- | :----- | :------------------------------------------------------------------------------- |
| `0x00` | `REG_IRQ_STATUS` | RO     | IRQ Line Status (Bit 0 = Timer). 4 Bytes.                                        |
| `0x04` | `REG_IRQ_MASK`   | RW     | IRQ Disable Mask.                                                                |
| `0x08` | `REG_IRQ_ACK`    | WO     | IRQ Acknowledge. Write `1` to Bit 0 to clear pending Timer IRQ. 4 Bytes.         |
| `0x0C` | `REG_TIMER_CTRL` | RW     | Timer Control: Bit 0=Enable Clock, Bit 1=IRQ Enable, Bit 2=Auto-Reload. 4 Bytes. |
| `0x10` | `REG_TIMER_TIME` | RO     | Free-running Clock. 64-bit timestamp (ns). 8 Bytes.                              |
| `0x18` | `REG_TIMER_CMP`  | RW     | Timer Alarm. Triggers IRQ when `TIME >= CMP`. 8 Bytes.                           |
