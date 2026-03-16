Overview
========
This project is no longer the stock NXP `hello_world` UART echo sample.
It has been repurposed to mirror the `TeensyC` firmware logic in a bare-metal
MCUX SDK application for the MIMXRT1062 used on Teensy 4.1.

The application now:

- configures Lepton CCI over `LPI2C1`
- captures VoSPI packets over `LPSPI4`
- rebuilds 160x120 frames
- streams framed chunks over `LPSPI3`
- exposes a USB CDC virtual COM port for status logs

SDK version
===========
- Version: 2.16.000

Toolchain supported
===================
- IAR embedded Workbench  9.60.1
- Keil MDK  5.39.0
- GCC ARM Embedded  13.2.1
- MCUXpresso  11.10.0

Hardware requirements
=====================
- Teensy 4.1
- FLIR Lepton module wired to the Teensy pin mapping used in `TeensyC`
- SPI receiver on the frame-link pins
- USB cable for flashing and USB CDC console

Pin mapping
===========
- `LPI2C1`: Teensy pins `19(SCL)` / `18(SDA)`
- `LPSPI4`: Teensy pins `10(CS)` / `12(MISO)` / `11(MOSI)` / `13(SCK)`
- `LPSPI3` frame link: Teensy pins `0(CS GPIO)` / `1(MISO)` / `26(MOSI)` / `27(SCK)`

Running the firmware
====================
1. Build the ARM GCC target in `armgcc`.
2. Flash the generated image to the Teensy-compatible RT1062 target.
3. Reconnect or wait for Windows to enumerate the Teensy as a USB virtual COM port.
4. Open the enumerated COM port at `115200 8N1`.
5. Verify logs such as `Lepton init complete` and `Frame sent over SPI`.

Notes
=====
- The board support files still come from the MCUX SDK `evkmimxrt1060` project
  because Teensy 4.1 uses the same RT1062 MCU.
- The generated MCUX Config Tools metadata from the original sample was removed
  because this project now uses manual pin configuration in code.
