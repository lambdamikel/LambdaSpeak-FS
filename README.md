# LambdaSpeak FS

## Latest News 

The first batch of 10 was produced and distributed by TFM! 

![LS FS Batch Front](images/batch1-front.jpg) 

![LS FS Batch Back](images/batch1-back.jpg) 

![LS FS Front](images/ls-fs-1.jpg) 

![LS FS Back](images/ls-fs-2.jpg) 

## Demos 

[YouTube Video 1](https://youtu.be/KedbNqoHSpE)

[YouTube Video 2](https://youtu.be/ffm2ckMMNg4)

[YouTube Video 3](https://youtu.be/5-QFHwXAVIw)


## About

"LambdaSpeak FutureSoft" aka "LambdaSpeak FS" aka "LS-FS" aka "LSFS". 

Check out [LambdaSpeak
3](https://github.com/lambdamikel/LambdaSpeak3) for
details. LambdaSpeak FS does not have the SP0256-AL2, and the
EEPROM-based (autonomous PCM sample-playing) functions are not
available either. 

But it has a serial high-speed connector for additional usage.
Moreover, it features a new `echo-byte` mode / control byte / mode 
`&C4` (yes, an explosive feature it is!). This mode allows the CPC to
take control over the LED Bar Segment Display, which is actually a 
full digital 8bit output port. It can be used as such by simply
removing the Segment Bar LED Display from the DIL socket and, e.g., by 
sticking DuPont connector cables into it (instead of the LEDs).

Another difference is optimized audio performance. Due to the absence
of the SP0256-AL2, we adopted a much simpler audio routing path,
omitting the audio op-amp for mixing, and optimized the filter
performance for a crystal clear and sharp digital sound. In
comparison, LambdaSpeak 3 sounds "warmer" and less clear. Somewhat
similar to an Amiga 500 with filter on / off (LSFS is filter off).
For PCM sound, this give the optimal performance in our opinion.

As with LambdaSpeak 3, the Amdrum / PCM sample playing mode is entered
by sending control byte `&E3`. A new feature is that the PCM mode can
be exited programmatically by sending the "exit sequence", without
having to power cycle the board. This sequence is `4 6 4 6 1 2 8`,
plus 127, so `131, 133, 131, 133, 128, 129, 135`.  However, checking
for this sequence in the stream of PCM sample bytes is expensive and
hence might reduce PCM sample quality slightly, so there is also a
mode in which the exit check is not being performed. See below.

Compared to LambdaSpeak 3, LSFS offers more control over the Amdrum /
PCM sample quality. LSFS features three different Amdrum modes. The
default is the `Standard` Amdrum mode.  This mode does a lightshow on
the LED Segment Bar, and also performs the exit check. The PCM
clipping range is set to 5, i.e., PCM bytes below 5 and above 250 will
be clipped and set to 5 resp. 250. 

The Amdrum mode / PCM sample quality can be specified as follows, by
first determining the mode, and then by using the mode by sending `&E3`
to enable the Amdrum mode: 

- `&FE`: Use Standard Amdrum mode. This
has a default PCM clipping range of 5, the lightshow is 
enabled, and the "exit check" is performed. 
- `&FD` : Use Custom Amdrum mode. When `&E3` is activated, 4 parameters have to
be specified and sent to port `&FBEE` first, then the mode is
enabled with these custom settings. These 4 bytes: first lightshow off / on (0, 1), 
second perform exit check no / yes (0, 1),  third PCM sample byte clipping range (from 0 to 127),  
and fourth PCM smoothing delta (from 0 to 127). 
- `&FC` : Use High Quality Amdrum mode. Like the Standard mode, but without lightshow and 
the exit check is not performed, hence resulting in highest PCM sample quality possible, 
as the CPC databus is sampled with highest frequency. 

## Serial Mode 

Some changes here over the LambdaSpeak 3 version. 

The serial mode uses a (ring or serial) buffer for buffering incoming
serial messages (bytes received over RX). By default, the buffer is
configured as a ring buffer, i.e., it starts again at 0 if it
overflows. Two pointers are used: a read cursor, and an input / fill
pointer. Both start at 0. If the read cursor is smaller than the input
pointer, then a byte is available. Bytes can be read from the buffer
until the pointers are equal again. 

There are two different transmission modes. The default should be the
*direct mode*. In this mode, every byte sent from the CPC to `&FBEE`
is output directly to the UART (TX). This mode can be used for full-duplex
communication, i.e., the ring buffer is being used for buffering incoming
serial bytes, and the output is not buffered, but output directly to the 
serial port. However, there is also the *buffered mode*, in which *output*
is not sent directly to the serial port. Rather, it is first put into 
the buffer, and upon a *flush buffer* command, the whole buffer is 
sent at once over the serial output (TX) port. Note that this mode
only allows *half-duplex*, since the input and output buffer is shared. 
This mode should be rarely needed, but it may result in higher serial 
transmission rates, because there is no delay in between sending serial 
bytes caused by the protocol or the CPC having to dispatch the bytes
to the interface. 

To control the UART / serial interface, sequences of control bytes are used,
and a control sequence starts with `255` / `&FF`. `255` can be be `escaped`
by sending `255` and then `255` again (so, to transmit `255` as a content
byte, send `255` twice).

The listener / command processing loop uses the READY byte 16 on the
IO port `&FBEE` to indicate that it is ready to receive the next
command or byte (note though, that the sub-mode 10 and 50 listeners do
not use the ready byte, see below). The byte 0 indicates that
LambdaSpeak 3 is busy.

The following table lists the command bytes in Serial Mode:

-------------------------------------------------------------------------------------------------------
| Byte Sequence   | Explanation                                   | Note                              |
|-----------------|-----------------------------------------------|-----------------------------------|
| 0...&FE         | Send Byte 0...254                             | Either buffered or TX directly    |
| &FF, &FF        | Send Byte 255                                 | Either buffered or TX directly    |
| &FF, 1, x       | Read x from bus and TX x                      | Transmit x directly to TX         |
| &FF, 2          | Send buffer to TX                             | Flush buffer, max 256 + 268 bytes |
| &FF, 3          | Get low byte number of bytes in input buffer  | Check if bytes have been received | 
| &FF, 4          | Get high byte number of bytes in input buffer | Check if bytes have been received | 
| &FF, 5          | Check if send/receive buffer is full          | 1 if full, 0 otherwise            | 
| &FF, 6          | Reset read and input cursors                  | Sets input & read cursors to 0    | 
| &FF, 7          | Check if another byte can be read from buffer | 1 if read cursor < input cursor   | 
| &FF, 8          | Get byte from buffer at read cursor position  | Byte will appear on databus       | 
| &FF, 9          | Get byte at read cursor position, inc. cursor | Read receive buffer byte by byte  | 
| &FF, 10         | SERIAL MONITOR SUB MODE FOR RX / SERIAL IN    | For example, realtime MIDI IN     |         
| &FF, 11, lo, hi | Set read cursor to position hi*256 + lo       | Use &FF, 8 to read byte at pos    | 
| &FF, 12         | Set read cursor to 0                          | Does not erase the buffer         |  
| &FF, 13         | Set read cursor to input cursor position -1   | Read cursor points to last byte   | 
| &FF, 14         | Get mode - direct or buffered mode            | 1 = direct mode, 0 = buffered     | 
| &FF, 15         | Speak mode (BAUD, Width, Parity, Stop Bits)   | Confirmations need to be enabled  | 
| &FF, 16         | Direct mode on                                | No CPC input buffering, direct TX | 
| &FF, 17         | Direct mode off                               | Buffer CPC input, then &FF, 2     | 
| &FF, 20         | Quit and reset Serial Mode                    | Like Reset Button                 | 
| &FF, 30, baud   | Set BAUD rate: baud = 0..15, see Baud Table   | Default 9600 (baud = 2, or > 15)  |   
| &FF, 31, width  | Set word width: width = 5...8                 | Default 8 bits                    | 
| &FF, 32, par.   | Set parity: 0, 1, 2                           | 0=No (Default), 1=Odd, 2=Even     | 
| &FF, 33, stop   | Set number of stop bits: 1, 2                 | 1 = Default                       | 
| &FF, 50         | SERIAL MONITOR SUB MODE RX AND TX (SERIAL IO) | For example, realtime MIDI IN/OUT |         
| &FF, 60         | Use Ring Buffer for Receiver (Default)        | Wrap around on overflow (Default) |         
| &FF, 70         | Use Linear Buffer for Receiver                | Stop on overflow (Default)        |         
| &FF, &C3        | Speak Current Mode Info                       | Same &C3 as in speech modes       | 
| &FF, &F2        | Get Mode Descriptor Byte                      | Same &F2 as in speech modes       | 
-------------------------------------------------------------------------------------------------------

Sub-modes 10 and 50 are meant for real-time serial (especially, MIDI)
*streaming*. Sub-mode 10 for MIDI IN input only (but echoes incoming
MIDI bytes automatically to TX = MIDI THROUGH), whereas sub-mode 50
works in both directions -- input and output (full duplex; MIDI IN /
MIDI OUT).

The **protocol for sub-mode 10** works as follows, and is identical to
the one for LambdaSpeak 3. To enter sub-mode 10, enter the serial
mode, select MIDI setting baud rates (8N1 and 31250 BAUD), and then
enter the mode via `OUT &FBEE,255: OUT &FBEE,10`. Then, the sub-mode
10 listener loop becomes active, and proceeds as described in the
LambdaSpeak 3 documentation.

The **protocol for sub-mode 50** is a bit more involved, as it
requires bi-directional communication. The protocol **is different
from the one currently implemented in LambdaSpeak 3**.  To enter
sub-mode 50, enter the serial mode, select MIDI setting baud rates
(8N1 and 31250 BAUD), and then enter the mode via `OUT &FBEE,255: OUT
&FBEE,50`. Then, the sub-mode 50 listener loop becomes active, and
proceeds as follows: 

1. LSFS listens to port `&FBEE` for incoming `<byte>`s. There are 4 command bytes: 
   - if `<byte> = 1`, then the next byte that is being sent from the CPC (to `&FBEE`) is forwarded to the serial interfact (i.e., is output over the TX line). Hence, to send byte `<byte>`, use `OUT &FBEE,3:OUT &FBEE,<byte>`. 
   - if `<byte> = 2`, then LSFS puts `1` or `0` on the databus, depending on whether a byte is available in the serial input buffer. The result (`1` or `0`) appears on the databus until the next command byte is sent from the CPC. 
   - if `<byte> = 3`, and a byte was available in the receive buffer, then LSFS first puts the lower nibble of this byte onto the databus. Please note that you need to ask first if a byte is available (i.e., using command `2`) before using command `3`! In order to distinguish the payload byte from the `0, 1` bytes, the lower nibble is shifted for bit positions to the left, and the 4 lower bits are set to one; hence, the lower nibble will appear as `<serial byte> & 0b00001111 ) << 4 | 0b00001111` on the databus. The smallest value will hence be `15`, encoding a `0` lower nibble. Next, to retrieve the **higher nibble** of the serial byte from the input buffer, the `3` command has to be sent again. This time, the *higher nibble* is put on the databus, and again, to ensure that `0, 1` cannot occur (or other synchronization bytes) and prevent protocol confusion, the lower 4 bits are set to one: `( <serial byte> & 0b11110000 ) | 0b00001111`.  Both the lower and the higher nibble are available on the databus until the next command byte is sent from the CPC. 
   - if `<byte> = 5`, then sub-mode 50 is exited, and LSFS returns to the main listener loop (not the serial listener loop). 

The sub-mode 10 and sub-mode 50 protocols are best understood by
looking at the provided MAXAM examplary assembler programs on the
[`LSFS.DSK`](cpc/lambda/LSFS.dsk) disk; see `MODE10.BAS` and
`MODE50.BAS`.

More details on [TFM's homepage.](http://futureos.cpc-live.com/) 

## Bill of Material

### Resistors 

- R7: 2k; ***note: must match D1 LED!*** 
- R1, R3, R4: 10k 
- R5: Jumper Wire 
- Rn: 9pin Resistor Array / Network, A102J

### Potentiometers / Trimmers: 

- RV1: 100 Ohm (Code 101) 
- RV2:  50k Ohm (Code 503) 

### Capacitors

- C1, C2: 22 pF (Code 220); ***note: these must match Q1!*** 
- C3: 68 nF (Code 683) 
- C5, C6, C7, C8: 100 nF (Code 104) 
- C9, C10: Jumper Wire 

### ICs 

- U1: GAL 22V10(B,D) 
- U2: 74LS374 
- U3: 74LS244 
- U4: ATmega 644-20PU
- Optional: DIL sockets for ICs (I recommend to use a socket at least for U4, to allow reprogramming) 

### Misc 

- CPC Connector: [2.54mm 2X25 50 Pin Right Angle Male Shrouded PCB Box Header IDC Connector](https://www.amazon.com/Madahu-Connectors-2-54mm-Shrouded-Connector/dp/B07XRH56QY) 
- Q1: [16 MHz Crystal](https://www.amazon.com/uxcell-Crystal-Oscillators-Resonators-Replacements/dp/B07Y7DVFCW/)
- SW3: [8 Position DIP Switch](https://www.amazon.com/Yohii-2-54mm-Positions-Double-Assorted/dp/B07DSBX4BK/) 
- D1: Activity LED 
- J8: [8 Segment LED Bar Display](https://www.amazon.com/Display-Segment-Graphics-Bar-Graph-8segmentos/dp/B07SMX1ZXX/)
- J2, J3: [2.54mm Pitch 8 Pin Single Row Straight Female Header Socket Connector for Arduino Shield (mikroBUS connector)](https://www.amazon.com/Comidox-2-54mm-Straight-Connector-Arduino/dp/B07J5B9LT5/)
- J7: Breakable Single Row Arduino Pin Headers 
- J4, J5: [Mini Stereo Audio Sockets](https://www.amazon.com/uxcell-Plastic-Stereo-Socket-Connector/dp/B00GLQAF7A/) 
- J1: [Optional Barrel Jack Power Connector](https://www.amazon.com/110PCS-2-1mm-Barrel-Type-Sockets-DC-005/dp/B073LF3FQK/)

### Modules 

- J2, J3: [TextToSpeech board](https://www.mikroe.com/text-to-speech-click) 
- MP3 / UART: [MP3 UART Module](https://www.amazon.com/Aideepen-YX5300-Control-Serial-Arduino/dp/B01JCI23JG) 
- RTC / I2C: [RTC IIC Module](https://www.amazon.com/Diymore-AT24C32-Arduino-Without-Battery/dp/B01IXXACD0)
- CR2032 3V Battery for RTC Module
- Mini SDCard for MP3 Module
- [Mini Stereo Audio Patch Cable](https://www.amazon.com/Tripp-Lite-P312-001-2RA-3-5mm-Stereo/dp/B00M5FKEUE/)


## Assembly Tipps 

- Don't forget the decoupling capacitors (104's) under the ICs! 
- If you are going to socket the ICs, then you will need to cut out the middle
struts from the DIL sockets to accomodate them.
- Use a fine side cutter to cut off the `SQW` and `32K` pins from the RTC module 

## Schematics

![Schematics](./schematics/schematics.png) 

See [here](./schematics/schematics.pdf)  for a PDF version. 

***Note that the Q, R, and C values should be taken from the above BOM instead, don't use the values from the schematics, some have been "optimized" by hand after the PCBs have been produced.***

Enjoy! 
