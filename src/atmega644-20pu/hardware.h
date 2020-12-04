//
// PINS 
// ================================  

//
// TTS Epson IC Part
// 

#define CS   PB3
#define MOSI PB5
#define MISO PB6
#define SCK  PB7

#define TTS_INPUT  PINB
#define TTS_OUTPUT PORTB
#define TTS_DDR    DDRB

#define S1V30120_RDY    PB0
#define S1V30120_RST    PB2
#define S1V30120_CS     PB3

//
//
//

// All OUTPUT, TTS ClickBoard control, except SCK, 
// and RDY = PB0 and _Z80_READY = PB1 INPUT!  -> 0 = INPUT, 1 = OUTPUT

#define CONFIGURE_TTS_INOUT DDRB  = 0b11111100 

//
// Z80 
// 

#define _Z80_READY PB1 
#define z80_run  DDRB = 0b11111100 
#define z80_halt DDRB = 0b11111110, clearBit(PORTB, _Z80_READY) 

//
// CPC Databus IO 
// 

#define FROM_CPC PINA
#define TO_CPC_2to7   PORTD 
#define TO_CPC_0to1   PORTC // PC5 = Bit 1, PC4 = Bit 0 

// output 
// PORTD 0 & 1 are RX & TX 
// Bits 7,6,5,4,3,2 : PORTD
// Bits 1, 0: PORTC PC5 PC4

#define CONFIGURE_TO_CPC    DDRD  |= 0b11111100; DDRC  |= 0b00110000 

// all input, no pullups 
#define CONFIGURE_FROM_CPC  DDRA  = 0b00000000; PORTA  = 0b00000000 

//
// Master DATABUS MACRO 
// 

// static volatile uint8_t synchro = 0; 

#define DATA_TO_CPC(arg)    { TO_CPC_2to7 = ( ( 0b11111100 & arg ) | ( TO_CPC_2to7 & 0b00000011)); TO_CPC_0to1 = ( ( ( 0b00000011 & arg ) << 4 ) | ( TO_CPC_0to1 & 0b11001111 )); }
#define DATA_FROM_CPC(arg)  arg = FROM_CPC 

//
// Virtual SSA1 PINs - MUST BE ON TO_CPC !
// 

#define _LRQ PD6 
#define  SBY PD7  

// #define NATIVE_SBY  PA5
// 2^PA5 = 32: &20

#define NATIVE_SBY_VAL 32  

#define SERIAL_SBY_VAL 16 

//
// DKtronics Signals 
// 

#define dk_speech_idle_loadme        DATA_TO_CPC(0) 
#define dk_speech_busy               setBit(TO_CPC_2to7, SBY)

//
// SSA1 Signals 
// 


#define speech_idle_loadme      clearBit(TO_CPC_2to7, _LRQ),   setBit(TO_CPC_2to7, SBY)  
#define speech_busy               setBit(TO_CPC_2to7, _LRQ), clearBit(TO_CPC_2to7, SBY)  
#define speech_speaking_loadme  clearBit(TO_CPC_2to7, _LRQ), clearBit(TO_CPC_2to7, SBY)  

//
// Native Signals - Can be used to determine if LambdaSpeak is still speaking, from CPC side 
// Only makes sense in non-blocking mode of course 
// In blocking mode, the CPC ist halted by pulling Z80 Wait to ground!
// 

#define speech_native_ready        DATA_TO_CPC(NATIVE_SBY_VAL) 
#define serial_ready               DATA_TO_CPC(SERIAL_SBY_VAL) 
#define speech_native_busy         DATA_TO_CPC(0) 
#define serial_busy                DATA_TO_CPC(0) 

//
// CPC Data WRite - From Address Decoder! Trigger 
// 

#define IOREQ_PIN      PINC
#define IOREQ_WRITE    PC7 
#define IOREQ_WRITE_DK PC7 

//
// Soft Reset Button and LEDs 
// 

// PC6 = D22 
#define SOFT_RESET_INT PCINT22 
#define SOFT_RESET_INT_VEC PCINT2_vect
#define SOFT_RESET_PIN PINC
#define SOFT_RESET_PIN_NUMBER PC6

// pullup for reset!
#define CONFIGURE_RESET PORTC |= 0b01000000 

//
//
// 

#define LED_PORT_READY    PORTC
#define LED_PORT_TRANSMIT PORTC

// PC1 = LED, PC0 = LED, 

#define READY_LED    PC3
#define TRANSMIT_LED PC3

// only PC6 and PC7 ARE INPUTS (RESET FROM BUTTON AND WRITE REQ FROM PAL) 

// 1 = OUTPUT 

#define CONFIGURE_LEDS DDRC = 0b00111111 

//
// PCM AUDIO 
//

#define PCM_PIN PB4 
#define CONFIGURE_PCM DDRB |= ( 1 << PB4); // = OC0B = PB4 = AMDRUM AUDIO OUTPUT

//
//
//

#define AMDRUM_PORT    PORTC 
#define AMDRUM_ENABLED PC2 

//
//
//

                                       /* SPI EEPROM 25LC256 Library */
#include <avr/io.h>

#define SPI_SS                     PB3
#define SPI_SS_PORT                PORTB
#define SPI_SS_PIN                 PINB
#define SPI_SS_DDR                 DDRB

#define SPI_MOSI                     PB5
#define SPI_MOSI_PORT                PORTB
#define SPI_MOSI_PIN                 PINB
#define SPI_MOSI_DDR                 DDRB

#define SPI_MISO                     PB6
#define SPI_MISO_PORT                PORTB
#define SPI_MISO_PIN                 PINB
#define SPI_MISO_DDR                 DDRB

#define SPI_SCK                     PB7
#define SPI_SCK_PORT                PORTB
#define SPI_SCK_PIN                 PINB
#define SPI_SCK_DDR                 DDRB

#define SLAVE_SELECT    SPI_SS_PORT &= ~(1 << SPI_SS)
#define SLAVE_DESELECT  SPI_SS_PORT |= (1 << SPI_SS)


// ================================
// END PINS 
// 
