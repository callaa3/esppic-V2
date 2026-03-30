#ifndef PIC_DEFS_H
#define PIC_DEFS_H

#define FLASHSIZE       8192
#define PICMAXWORDS     8192
#define PICBLANKWORD    0x3FFF

// Locations of the special Config & Id memories on the PIC
#define USERID    0
#define CONFIG1   7
#define CONFIG2   8
#define DEVID     6
#define DEVREV    5

#define DLY1  10      // 10 microseconds for toggling and stuff
#define DLY2  10000   // 10  millisecond for command delay

// ESP32 GPIO pin mapping
#define PIN_RESET 27   // MCLR/VPP control (active HIGH = MCLR low via NPN transistor)
#define PIN_DAT   25   // ICSPDAT (PGD)
#define PIN_CLK   26   // ICSPCLK (PGC)
#define PIN_VPP   14   // VPP control via NPN shunt (LOW = VPP reaches MCLR, HIGH = VPP clamped)

#endif
