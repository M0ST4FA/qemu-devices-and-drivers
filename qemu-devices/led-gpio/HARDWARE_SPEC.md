# LuxPCIe Gen 1: Dual-Mode LED/GPIO Controller

## Overview
The LuxPCIe Gen 1 is a virtual PCIe device designed to demonstrate two paradigms of Linux kernel hardware interaction: generic subsystem reuse and custom driver development.

### PCI Identity
*   **Vendor ID:** `0x1234` (Experimental)
*   **Device ID:** `0x0001`
*   **Class Code:** `0x0880` (Generic System Peripheral)

---

## Memory Map

### BAR 0: Generic GPIO Interface
*   **Size:** 4096 Bytes (4KB)
*   **Purpose:** Compatibility with the standard Linux `gpio-mmio` (`bgpio_init`) driver.
*   **Semantic:** Provides a 1-bit binary interface for 64 LED pins.

| Offset | Name         | Access | Description |
| :---   | :---         | :---   | :--- |
| `0x00` | `REG_MAGIC`  | RO     | Magic: `0x4750494F` ("GPIO"). |
| `0x04` | `REG_VER`    | RO     | Version: `0x00000001`. |
| `0x08` | `REG_DIR`    | RW     | Direction (1=Out, 0=In). Default: `0x0`. |
| `0x0C` | `REG_DATA`   | RW     | Data. Read/Write current pin states. |
| `0x10` | `REG_SET`    | WO     | Atomic Set. Write `1` to a bit to set pin HIGH. |
| `0x14` | `REG_CLR`    | WO     | Atomic Clear. Write `1` to a bit to set pin LOW. |

---

### BAR 1: Smart LED Controller Interface
*   **Size:** 4096 Bytes (4KB)
*   **Purpose:** Custom driver development for "Smart" features (Color/Brightness).
*   **Semantic:** An array of 512-byte total (supports 64 LEDs).

Each LED is mapped to an 8-byte structure:
```c
struct smart_led {
    uint32_t state; // Bit 0: ON/OFF, Bits 1-31: Reserved
    uint32_t color; // 32-bit ARGB (8:8:8:8)
};
```

| Offset Range | Name         | Access | Description |
| :---         | :---         | :---   | :--- |
| `i*8 + 0x0`  | `LED[i].ST`  | RW     | State of LED `i`. |
| `i*8 + 0x4`  | `LED[i].COL` | RW     | Color of LED `i`. |

---

## Architectural Rationale

1.  **Dual Visibility:** BAR 0 and BAR 1 are different "views" of the same internal hardware state. A write to `REG_SET` in BAR 0 will be reflected in `LED[i].ST` in BAR 1.
2.  **Alignment:** 8-byte alignment for Smart LEDs ensures atomic 64-bit writes or efficient 32-bit access without cache line splitting.
3.  **Extensibility:** 4KB BARs provide ample room for future registers (e.g., hardware-based animations or PWM control).
4.  **Standardization:** BAR 0 allows immediate testing using the `gpio-mmio` driver without writing a single line of kernel code.
