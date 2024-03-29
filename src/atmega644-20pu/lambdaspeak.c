/* 
   LambdaSpeak - A Next-Generation Speech Synthesizer for the CPC 
   Firmware for LambdaSpeak FS Edition
   Copyright (C) 2017 - 2021 Michael Wessel 

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

   LambdaSpeak FS Edition
   Copyright (C) 2017 - 2021 Michael Wessel
   LambdaSpeak comes with ABSOLUTELY NO WARRANTY. 
   This is free software, and you are welcome to redistribute it
   under certain conditions. 

*/ 

//
// LambdaSpeak FS
// Version 10 
// License: GPL 3 
// 
// (C) 2021 Michael Wessel 
// mailto:miacwess@gmail.com
// https://www.michael-wessel.info
// 

/*
IMPORTANT LICENSE INFORMATION: 
LambdaSpeak uses GPL 3. 
This code uses the Epson S1V30120 firmware image from the TextToSpeech 
click board library from MikroElektronika released under GPL2: 
https://github.com/MikroElektronika/Click_TextToSpeech_S1V30120/blob/master/library/include/text_to_speech_img.h

By using this code, you are also bound to the Epson license terms for the S1V30120 firmware:
https://global.epson.com/products_and_drivers/semicon/products/speech/voice/sla/s1v30120_init_data.html
*/ 

#include <avr/pgmspace.h>
#include <avr/io.h>

#include <util/delay.h>
#include <util/atomic.h>
#include <avr/interrupt.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <avr/wdt.h>
#include <string.h>

#include "S1V30120_defines.h" 
#include "text_to_speech_img.h"

// #define CPC_READ_DELAY  FAST_CPC_GETTERS ? _delay_us(150) :  _delay_ms(10) 
// static volatile uint8_t FAST_GETTER_DELAY_US = 150; 
// static volatile uint8_t SLOW_GETTER_DELAY_MS = 10; 

static inline void delay_us(unsigned int microseconds) __attribute__((always_inline));
void delay_us(unsigned int microseconds)
{
  __asm__ volatile (
		    "1: push r22"     "\n\t"
		    "   ldi  r22, 4"  "\n\t"
		    "2: dec  r22"     "\n\t"
		    "   brne 2b"      "\n\t"
		    "   pop  r22"     "\n\t"
		    "   sbiw %0, 1"   "\n\t"
		    "   brne 1b"
		    : "=w" ( microseconds )
		    : "0" ( microseconds )
		    );
}




#define FAST_GETTER_DELAY_US 10
#define MEDIUM_GETTER_DELAY_US 500
#define SLOW_GETTER_DELAY_MS 20

#define loop_until_write_happened { WRITE_HAPPENED = 0; while ( !WRITE_HAPPENED) {   __asm__ volatile ("NOP"); }; }

#define CPC_READ_DELAY if ( FAST_CPC_GETTERS == 1 ) _delay_us(FAST_GETTER_DELAY_US); else if ( FAST_CPC_GETTERS == 2 ) _delay_us(MEDIUM_GETTER_DELAY_US); else if ( FAST_CPC_GETTERS == 0 ) _delay_ms(SLOW_GETTER_DELAY_MS); else loop_until_write_happened 

#define _NOP() do { __asm__ __volatile__ ("nop"); } while (0)
// used for a very short delay

// #define BOOTMESSAGE
 
#define VERSION 10 

#include "HAL9000_defines.h"    

#include "hardware.h"
//#include "pcm-future.h"
//#include "pcm3.h"
#include "pcm.h"

//
//
//

#define configure_speak_buffer_timer  TCCR0A = 0; TCCR0B = 0; setBit(TCCR0A, WGM01); OCR0A = 0xF9; setBit(TIMSK0, OCIE0A)

#define start_timer emulated_ssa1_dktronics_ms_counter = 0; setBit(TCCR0B, CS02); setBit(TCCR0B, CS00)

#define stop_timer  TCCR0B &= ~(1 << CS02); TCCR0B &= ~(1 << CS00) 

//
// CPC Databus 
// 

static volatile uint8_t databus = 0; 
static volatile uint8_t databus1 = 0; 
static volatile uint8_t databus2 = 0; 

static volatile uint8_t  byte_avail = 0; 
static volatile uint16_t bytes_available = 0; 

static volatile uint8_t SERIAL_BYTE_AVAILABLE = 0; 
static volatile uint8_t SERIAL_BYTE_RECEIVED = 0; 

static volatile uint8_t WRITE_HAPPENED = 0; 

//
// Speech Buffer
// 
 
static char command_string[188];  // longest string in HAL quotes and copyright messages

// for all communication to Epson chip
// ALSO, for LS3, this is the USART BUFFER SIZE! 

//  #define SEND_BUFFER_SIZE 280

#define SEND_BUFFER_SIZE 268 // 256 + 8 extra bytes for headers

// for Input Buffering from CPC: 

#define SPEECH_BUFFER_SIZE 256 
#define SPEECH_BUFFER_FLUSH_AT 253 // - 3 of SPEECH_BUFFER_SIZE 

#define TOTAL_BUFFER_SIZE 524 // 268 + 256 

//
// Buffers 
// 

static volatile uint8_t length = 0; 
static volatile uint8_t buffer[SPEECH_BUFFER_SIZE] = { 0 }; 

static char rcvd_msg[20] = { 0 };
static char send_msg[SEND_BUFFER_SIZE] = { 0 };

//
//
//

static volatile uint8_t usart_input_buffer_index = 0; // receive input from USART

static volatile uint8_t cpc_read_cursor = 0;  // reading USART received buffer from CPC 

// this is one is really 16 bit: 
static volatile uint16_t from_cpc_input_buffer_index = 0;  // input from CPC to send to USART


//
// Flow Control 
// 

static volatile uint8_t BLOCKING = 1;  
static volatile uint8_t NON_BLOCK_CONFIRMATIONS = 0;  
static volatile uint8_t STOP_NOW = 0; 

#define WAIT_UNTIL_FINISHED { STOP_NOW = 0; while ( ! tts_parse_response(ISC_TTS_FINISHED_IND, 0x0000, 16)) {  if (  ! STOP_NOW && ( bit_is_set(IOREQ_PIN, IOREQ_WRITE) ||  bit_is_set(IOREQ_PIN, IOREQ_WRITE_DK)) && FROM_CPC == 0xDF ) { STOP_NOW = 1; break; } } }

//
//
//

static uint8_t rtc_seconds;
static uint8_t rtc_minutes;
static uint8_t rtc_hours;
static uint8_t rtc_month;
static uint8_t rtc_year; 
static uint8_t rtc_date;
static uint8_t rtc_weekday; 

static uint8_t rtc_msb_temp;

static uint8_t rtc_hours_dec;
static uint8_t rtc_mins_dec; 
static uint8_t rtc_secs_dec;
static uint8_t rtc_year_dec;
static uint8_t rtc_month_dec;
static uint8_t rtc_date_dec;
static uint8_t rtc_weekday_dec; 


//
// Utilities 
// 

#define BV(bit) (1 << (bit))
#define toggleBit(byte, bit)  (byte ^= BV(bit))
#define setBit(byte, bit) (byte |= BV(bit)) 
#define clearBit(byte, bit) (byte &= ~BV(bit))

#ifndef LSBFIRST
#define LSBFIRST 0
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#endif

#define SPI_CLOCK_DIV4 0x00
#define SPI_CLOCK_DIV16 0x01
#define SPI_CLOCK_DIV64 0x02
#define SPI_CLOCK_DIV128 0x03
#define SPI_CLOCK_DIV2 0x04
#define SPI_CLOCK_DIV8 0x05
#define SPI_CLOCK_DIV32 0x06

#define SPI_MODE0 0x00
#define SPI_MODE1 0x04
#define SPI_MODE2 0x08
#define SPI_MODE3 0x0C // 00001100  

#define SPI_MODE_MASK 0x0C  // CPOL = bit 3, CPHA = bit 2 on SPCR
#define SPI_CLOCK_MASK 0x03  // SPR1 = bit 1, SPR0 = bit 0 on SPCR
#define SPI_2XCLOCK_MASK 0x01  // SPI2X = bit 0 on SPSR

//
//
// 

static volatile unsigned short TTS_DATA_IDX;

static volatile unsigned short msg_index;

static volatile unsigned short msg_len;
static volatile unsigned short txt_len;

//
// LambdaSpeak part 
// 

#define BV(bit) (1 << (bit))
#define toggleBit(byte, bit)  (byte ^= BV(bit))
#define setBit(byte, bit) (byte |= BV(bit))
#define clearBit(byte, bit) (byte &= ~BV(bit))

// 
//
// 

#define TRANSMIT_ON   setBit(LED_PORT_TRANSMIT, TRANSMIT_LED) 
#define READY_ON      setBit(LED_PORT_READY, READY_LED) 

#define TRANSMIT_OFF  clearBit(LED_PORT_TRANSMIT, TRANSMIT_LED) 
#define READY_OFF     clearBit(LED_PORT_READY, READY_LED) 

#define LEDS_OFF { TRANSMIT_OFF; READY_OFF; }
#define LEDS_ON  { TRANSMIT_ON; READY_ON; }

//
//
//

#define SP0256_SSA1_ON  
#define SP0256_DK_ON    
#define SP0256_OFF      

#define EPSON_SSA1_ON  
#define EPSON_DK_ON    

#define LAMBDA_EPSON_ON 
#define LAMBDA_DECTALK_ON

#define SERIAL_ON  


// 9600 at 8N1  

static volatile uint32_t SERIAL_RATE = 0; 
static volatile uint8_t SERIAL_BAUDRATE = 2; 
static volatile uint8_t SERIAL_WIDTH = 8; 
static volatile uint8_t SERIAL_PARITY = 0; 
static volatile uint8_t SERIAL_STOP_BITS = 1; 

//
//
//

#define AMDRUM_ON AMDRUM_PORT = 0; setBit(AMDRUM_PORT, AMDRUM_ENABLED) 

//
// Status   
// 
 
static volatile int FAST_CPC_GETTERS = 0;  // for CPC_READ_DELAY : 0 = BASIC / SLOW, 1 = MC / FAST, 2 = MEDIUM 

typedef enum { SSA1_M = 0, LAMBDA_EPSON_M = 1, LAMBDA_DECTALK_M = 2, DKTRONICS_M = 3, AMDRUM_M = 4, SERIAL_M = 9, START_OVER_SAME_MODE = 10 } LS_MODE; 

// static volatile LS_MODE CUR_MODE = SSA1_M; 
// static volatile LS_MODE LAST_MODE = SSA1_M;  

static volatile LS_MODE CUR_MODE = LAMBDA_EPSON_M; 
static volatile LS_MODE LAST_MODE = LAMBDA_EPSON_M; 

typedef enum { AMDRUM_STD = 0, AMDRUM_CUSTOM = 1, AMDRUM_HQ = 2} PCM_MODE; 

static volatile PCM_MODE CUR_AMDRUM_MODE = AMDRUM_STD; 

// 
//
//

#define VOLUME_DEFAULT 13
#define SPEAK_RATE_DEFAULT 9
#define FLUSH_BUFFER_DELAY_DEFAULT 10 

static volatile uint8_t VOICE = ISC_TTS_VOICE + 1; 
static volatile uint8_t SPEAK_RATE = SPEAK_RATE_DEFAULT; 
static volatile uint8_t LANGUAGE = ISC_TTS_LANGUAGE;  
static volatile uint8_t VOLUME = VOLUME_DEFAULT; 
static volatile uint8_t CONFIRM_COMMANDS = 1; 
static volatile uint8_t FLUSH_BUFFER_DELAY = FLUSH_BUFFER_DELAY_DEFAULT;  

//
// Buffer 
// 

#define SSA1_DKTRONICS_FLUSH_BUFFER_AFTER_MS 110 
#define SSA_DKTRONICS_LOADME_AGAIN_AFTER_MS 14

static volatile uint8_t emulated_ssa1_buffer_size = 0; 
static volatile uint8_t emulated_ssa1_dktronics_ms_counter = 0; 

//
//
// 

// static uint16_t allo_length[0x40]; 
static char*    allo_map[0x40]; 

//
//
// 

static volatile uint8_t ssa1_and_dk_flush_buffer_after_ms = SSA1_DKTRONICS_FLUSH_BUFFER_AFTER_MS;
static volatile uint8_t ssa_dktronics_loadme_again_after_ms = SSA_DKTRONICS_LOADME_AGAIN_AFTER_MS;


//
//
// 

inline static void setBitOrder(uint8_t bitOrder) {
  if (bitOrder == LSBFIRST) SPCR |= _BV(DORD);
  else SPCR &= ~(_BV(DORD));
}

inline static void setDataMode(uint8_t dataMode) {
  SPCR = (SPCR & ~SPI_MODE_MASK) | dataMode;
}

inline static void setClockDivider(uint8_t clockDiv) {
  SPCR = (SPCR & ~SPI_CLOCK_MASK) | (clockDiv & SPI_CLOCK_MASK);
  SPSR = (SPSR & ~SPI_2XCLOCK_MASK) | ((clockDiv >> 2) & SPI_2XCLOCK_MASK);
}

/* 
static inline void delay_us(unsigned int microseconds) __attribute__((always_inline));
void delay_us(unsigned int microseconds)
{
  __asm__ volatile (
		    "1: push r22"     "\n\t"
		    "   ldi  r22, 4"  "\n\t"
		    "2: dec  r22"     "\n\t"
		    "   brne 2b"      "\n\t"
		    "   pop  r22"     "\n\t"
		    "   sbiw %0, 1"   "\n\t"
		    "   brne 1b"
		    : "=w" ( microseconds )
		    : "0" ( microseconds )
		    );
}

*/ 

//
//
// 

ISR(TIMER1_OVF_vect) { }

ISR(TIMER2_OVF_vect) { } 

ISR(TIMER2_COMPA_vect) { } 

ISR(TIMER0_OVF_vect) { }

/* 

   ISR(TIMER2_OVF_vect) {

   if (playing1) {    
   pcm_counter1++;     
   if ( pcm_counter1 >= pcm1_speed) {
   pcm_counter1 = 0; 
   pcm1_address++;
   }
   }

   if (playing2) {    
   pcm_counter2++;     
   if ( pcm_counter2 >= pcm2_speed) {
   pcm_counter2 = 0; 
   pcm2_address++;
   }
   }

   if (playing3) {    
   pcm_counter3++;     
   if ( pcm_counter3 >= pcm3_speed) {
   pcm_counter3 = 0; 
   pcm3_address++;
   }
   }

   if (playing4) {    
   pcm_counter4++;     
   if ( pcm_counter4 >= pcm4_speed) {
   pcm_counter4 = 0; 
   pcm4_address++;
   }
   }

   }

   ISR(TIMER2_COMPA_vect) {

   if (playing1) {    
   pcm_counter1++;     
   if ( pcm_counter1 >= pcm1_speed) {
   pcm_counter1 = 0; 
   pcm1_address++;
   }
   }

   if (playing2) {    
   pcm_counter2++;     
   if ( pcm_counter2 >= pcm2_speed) {
   pcm_counter2 = 0; 
   pcm2_address++;
   }
   }

   if (playing3) {    
   pcm_counter3++;     
   if ( pcm_counter3 >= pcm3_speed) {
   pcm_counter3 = 0; 
   pcm3_address++;
   }
   }

   if (playing4) {    
   pcm_counter4++;     
   if ( pcm_counter4 >= pcm4_speed) {
   pcm_counter4 = 0; 
   pcm4_address++;
   }
   }

   }

*/


//
//
//

#define soft_reset() do { SLAVE_DESELECT; wdt_enable(WDTO_15MS); for(;;) {}} while(0) 

void process_reset(void) {
  soft_reset(); 
}

//
//
// 

ISR(SOFT_RESET_INT_VEC) { // reset handler 
  WRITE_HAPPENED++;
  if (bit_is_clear(SOFT_RESET_PIN, SOFT_RESET_PIN_NUMBER)) {
    process_reset();  
  }  
}
  
//
//
// 

void init_reset_handler(void) {

  setBit(PCICR, PCIE2); // change interrupts on 
  
  // setBit(PCMSK2, SOFT_RESET_INT); // RESET PIN = PC6 = D22 

  // PCMSK2 |= (1 << PCINT23) | (1 << SOFT_RESET_INT);  

  setBit(PCMSK2, SOFT_RESET_INT); // RESET PIN = PC6 = D22  
  // setBit(PCMSK2, PCINT23); // IOREQ WRITE PIN CHANGE INTERRUPT

  WRITE_HAPPENED = 0; 

}

void enable_write_isr(void) {

  setBit(PCMSK2, PCINT23); // IOREQ WRITE PIN CHANGE INTERRUPT
  WRITE_HAPPENED = 0; 

}

void disable_write_isr(void) {

  clearBit(PCMSK2, PCINT23); // IOREQ WRITE PIN CHANGE INTERRUPT
  WRITE_HAPPENED = 0; 

}


void wdt_init(void)
{

  cli();

  wdt_reset();
  MCUSR &= ~(1 << WDRF);
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  WDTCSR = 0x00;

  sei();
   
  return;

}  

//
// SPI Epson TTS 
// 

void spi_begin_epson(void) { 

  setBit(TTS_OUTPUT, S1V30120_CS); // S1V30120 not selected

  LAMBDA_EPSON_ON; 
  
  // make the MOSI, SCK, and CS pins outputs
  TTS_DDR |= ( 1 << MOSI ) | ( 1 << SCK ) | ( 1 << CS );
 
  // make sure the MISO pin is input
  TTS_DDR &= ~( 1 << MISO );
  // pullup on MISO? 
  SPI_MISO_PORT |= (1 << SPI_MISO);

  setBitOrder(MSBFIRST); 
  setDataMode(SPI_MODE3);
  setClockDivider(SPI_CLOCK_DIV2); 
  // setClockDivider(SPI_CLOCK_DIV8); 
  //setClockDivider(SPI_CLOCK_DIV32); 

  SPCR |= ( 1 << SPE ) | ( 1 << MSTR ); 
  
} 

//Function to send and receive data for both master and slave
unsigned char spi_transceive_epson (unsigned char data)
{
  // Load data into the buffer
  SPDR = data;
 
  //Wait until transmission complete
  while(!(SPSR & (1<<SPIF) ));

  _delay_us(3); 
 
  // Return received data
  return(SPDR);
}

void spi_transfer_epson(unsigned char data) {

  spi_transceive_epson(data); 

}

void spi_begin_epson_transaction(void) {

}

void spi_end_epson_transaction(void) {

}

//
//
// 

void blink_led_fast(int n) {

  for (int i = 0; i < n; i++) {

    setBit(LED_PORT_READY, READY_LED); 
    setBit(LED_PORT_TRANSMIT, TRANSMIT_LED); 

    _delay_ms(10); 

    clearBit(LED_PORT_READY, READY_LED); 
    clearBit(LED_PORT_TRANSMIT, TRANSMIT_LED); 

    _delay_ms(10);             

  }

  _delay_ms(50); 

}

void blink_led_slow(int n) {

  for (int i = 0; i < n; i++) {

    setBit(LED_PORT_READY, READY_LED); 
    setBit(LED_PORT_TRANSMIT, TRANSMIT_LED); 

    _delay_ms(200); 

    clearBit(LED_PORT_READY, READY_LED); 
    clearBit(LED_PORT_TRANSMIT, TRANSMIT_LED); 

    _delay_ms(200);             

  }

  _delay_ms(1000); 

}

void blink_leds(void) {
  blink_led_fast(10); 
}


void check_for_error(int response)
{
  if (response) 
    return; 
  else
    {
      blink_led_slow(10); 
      process_reset();      
    }
  
}

//
//
// 


void tts_reset(void)
{

  setBit(TTS_OUTPUT, S1V30120_CS); // S1V30120 not selected
  clearBit(TTS_OUTPUT, S1V30120_RST);

  spi_begin_epson_transaction();

  // send one dummy byte, this will leave the clock line high
  // SPI.beginTransaction(SPISettings(750000, MSBFIRST, SPI_MODE3));

  spi_transfer_epson(0x00);

  spi_end_epson_transaction();

  _delay_ms(5); 

  setBit(TTS_OUTPUT, S1V30120_RST);

  _delay_ms(150);

}

void tts_send_message(char message[], unsigned char message_length) {
  // Check to see if there's an incoming response or indication

  while( bit_is_set(TTS_INPUT, S1V30120_RDY) );  // blocking
  // OK, we can proceed
  
  clearBit(TTS_OUTPUT, S1V30120_CS);

  spi_begin_epson_transaction(); 

  spi_transfer_epson(0xAA);  // Start Message Command
  for (int i = 0; i < message_length; i++)
    {
      spi_transfer_epson(message[i]); 
    }  

  spi_end_epson_transaction(); 


}

uint16_t convert_speak_rate(uint8_t rate) {  
  return 15 + 25 * rate; 
} 

uint8_t convert_volume(uint8_t vol) {  
  return 7 + 4 * vol; 
} 

// Padding function
// Sends a num_padding_bytes over the SPI bus
void tts_send_padding(unsigned short num_padding_bytes)
{
  for (int i = 0; i < num_padding_bytes; i++)
    {
      spi_transfer_epson(0x00);
    }  
}

unsigned short tts_get_version(void)
{
  // Query version
  unsigned short S1V30120_version = 0;

  // Sending ISC_VERSION_REQ = [0x00, 0x04, 0x00, 0x05];
  char msg_ver[] = {0x04, 0x00, 0x05, 0x00};

  tts_send_message(msg_ver, 0x04);    
  //wait for ready signal

  while( bit_is_clear(TTS_INPUT, S1V30120_RDY));
    
  // receive 20 bytes
  clearBit(TTS_OUTPUT, S1V30120_CS); 

  spi_begin_epson_transaction(); 
  // wait for message start
  while(spi_transceive_epson(0x00) != 0xAA);
  for (int i = 0; i < 20; i++)
    {
      rcvd_msg[i] = spi_transceive_epson(0x00);
    }
  // Send 16 bytes padding

  tts_send_padding(16);  
  spi_end_epson_transaction(); 

  setBit(TTS_OUTPUT, S1V30120_CS);

  S1V30120_version = rcvd_msg[4] << 8 | rcvd_msg[5];

  return S1V30120_version;

}



int tts_parse_response(unsigned short expected_message, unsigned short expected_result, unsigned short padding_bytes)
{
  unsigned short rcvd_tmp; 

  //wait for ready signal
  while( bit_is_clear(TTS_INPUT, S1V30120_RDY) ) { if ( ! STOP_NOW && ( bit_is_set(IOREQ_PIN, IOREQ_WRITE) || bit_is_set(IOREQ_PIN, IOREQ_WRITE_DK)) && FROM_CPC == 0xDF ) { STOP_NOW = 1; return 1; } }; 

  // receive 6 bytes
  clearBit(TTS_OUTPUT, S1V30120_CS);

  spi_begin_epson_transaction(); 

  // wait for message start
  // check for stop!
  while ( spi_transceive_epson(0x00) != 0xAA) { if (  ! STOP_NOW && ( bit_is_set(IOREQ_PIN, IOREQ_WRITE) || bit_is_set(IOREQ_PIN, IOREQ_WRITE_DK)) && FROM_CPC == 0xDF ) { STOP_NOW = 1; return 1; } }; 

  for (int i = 0; i < 6; i++) {
    rcvd_msg[i] = spi_transceive_epson(0x00);
  }

  tts_send_padding(padding_bytes); 
  spi_end_epson_transaction(); 

  setBit(TTS_OUTPUT, S1V30120_CS);

  // Are we successfull? We shall check 
  rcvd_tmp = rcvd_msg[3] << 8 | rcvd_msg[2];
  if (rcvd_tmp == expected_message) // Have we received ISC_BOOT_RUN_RESP?
    { 
      // We check the response
      rcvd_tmp = rcvd_msg[5] << 8 | rcvd_msg[4];
      if (rcvd_tmp == expected_result) // success, return 1
	return 1;
      else
	return 0;
    }
  else // We received something else
    return 0;

}


int tts_configure_audio(void) {

  msg_len = 0x0C;

  send_msg[0] = msg_len & 0xFF;          // LSB of msg len
  send_msg[1] = (msg_len & 0xFF00) >> 8; // MSB of msg len
  send_msg[2] = ISC_AUDIO_CONFIG_REQ & 0xFF;
  send_msg[3] = (ISC_AUDIO_CONFIG_REQ & 0xFF00) >> 8;
  send_msg[4] = TTS_AUDIO_CONF_AS;

  send_msg[5] = convert_volume(VOLUME); 
  // send_msg[5] = TTS_AUDIO_CONF_AG; 

  send_msg[6] = TTS_AUDIO_CONF_AMP;
  send_msg[7] = TTS_AUDIO_CONF_ASR;
  send_msg[8] = TTS_AUDIO_CONF_AR;
  send_msg[9] = TTS_AUDIO_CONF_ATC;
  send_msg[10] = TTS_AUDIO_CONF_ACS;
  send_msg[11] = TTS_AUDIO_CONF_DC;

  tts_send_message(send_msg, msg_len); 

  return tts_parse_response(ISC_AUDIO_CONFIG_RESP, 0x0000, 16); 

}

int tts_registration(void)
{

  spi_begin_epson_transaction(); 

  char reg_code[] = {0x0C, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  tts_send_message(reg_code, 0x0C);

  return tts_parse_response(ISC_TEST_RESP, 0x0000, 16);

}    

int tts_load_chunk(unsigned short chunk_len)
{
  // Load a chunk of data
  char len_msb = ((chunk_len + 4) & 0xFF00) >> 8;
  char len_lsb = (chunk_len + 4) & 0xFF;

  clearBit(TTS_OUTPUT, S1V30120_CS);

  spi_begin_epson_transaction(); 

  spi_transfer_epson(0xAA);  // Start Message Command
  spi_transfer_epson(len_lsb);  // Message length is 2048 bytes = 0x0800
  spi_transfer_epson(len_msb);  // LSB first
  spi_transfer_epson(0x00);  // Send SC_BOOT_LOAD_REQ (0x1000)
  spi_transfer_epson(0x10);
  for (int chunk_idx = 0; chunk_idx < chunk_len; chunk_idx++)
    {
      //spi_transfer_epson(TTS_INIT_DATA[TTS_DATA_IDX]);
      unsigned char x = pgm_read_byte(&(TTS_INIT_DATA[TTS_DATA_IDX])); 
      spi_transfer_epson(x); 

      TTS_DATA_IDX++;
    }   

  spi_end_epson_transaction(); 

  setBit(TTS_OUTPUT, S1V30120_CS);

  return tts_parse_response(ISC_BOOT_LOAD_RESP, 0x0001, 16);

}



int tts_download(void)
{
  // TTS_INIT_DATA is of unsigned char type (one byte)
  unsigned short len = sizeof (TTS_INIT_DATA);

  unsigned short fullchunks;
  unsigned short remaining;

  int chunk_result;

  // We are loading chunks of data
  // Each chunk, including header must be of maximum 2048 bytes
  // as the header is 4 bytes, this leaves 2044 bytes to load each time
  // Computing number of chunks
  fullchunks = len / 2044;
  remaining = len - fullchunks * 2044;

  // Load a chunk of data
  for (int num_chunks = 0; num_chunks < fullchunks; num_chunks++)
    {
      chunk_result = tts_load_chunk (2044);
      if (chunk_result)
	{
       
	}  
      else
	{
	  return 0;
	}
    }
  // Now load the last chunk of data  
  chunk_result = tts_load_chunk (remaining);
  if (chunk_result)
    {
     
    }  
  else
    {
      return 0;
    }
  // All was OK, returning 1
  return 1;   
}

int tts_boot_run(void)
{
  char boot_run_msg[] = {0x04, 0x00, 0x02, 0x10};
  tts_send_message(boot_run_msg, 0x04);
  return tts_parse_response(ISC_BOOT_RUN_RESP, 0x0001, 8);
}


int tts_speech(char* text_to_speech, unsigned char flush_enable)
{
  int response;

  txt_len = strlen(text_to_speech); 
  msg_len = txt_len + 6;

  send_msg[0] = msg_len & 0xFF;          // LSB of msg len
  send_msg[1] = (msg_len & 0xFF00) >> 8; // MSB of msg len
  send_msg[2] = ISC_TTS_SPEAK_REQ & 0xFF;
  send_msg[3] = (ISC_TTS_SPEAK_REQ & 0xFF00) >> 8;
  send_msg[4] = flush_enable; // flush control

  for (int i = 0; i < txt_len; i++)
    {
      send_msg[i+5] = text_to_speech[i];
    }

  send_msg[msg_len-1] = '\0'; // null character
  tts_send_message(send_msg, msg_len);

  response = tts_parse_response(ISC_TTS_SPEAK_RESP, 0x0000, 16); 
  
  WAIT_UNTIL_FINISHED; 
  
  return response; 

}

int tts_speech_progmem(PGM_P flash_address, unsigned char flush_enable)
{

  int response;
  int msg_len = 0; 
  char c = 0; 

  do {
    c = (char) pgm_read_byte(flash_address++);
    send_msg[msg_len + 5] = c; 
    msg_len++; 
  } while (c!='\0');
    
  msg_len += 5; 
  send_msg[0] = msg_len & 0xFF;          // LSB of msg len
  send_msg[1] = (msg_len & 0xFF00) >> 8; // MSB of msg len
  send_msg[2] = ISC_TTS_SPEAK_REQ & 0xFF;
  send_msg[3] = (ISC_TTS_SPEAK_REQ & 0xFF00) >> 8;
  send_msg[4] = flush_enable; // flush control

  tts_send_message(send_msg, msg_len); 

  response = tts_parse_response(ISC_TTS_SPEAK_RESP, 0x0000, 16); 

  WAIT_UNTIL_FINISHED; 

  return response; 

}

void tts_speech_start(unsigned char flush_enable)
{
  send_msg[0] = 0; // length to be set later!
  send_msg[1] = 0; 
  send_msg[2] = ISC_TTS_SPEAK_REQ & 0xFF;
  send_msg[3] = (ISC_TTS_SPEAK_REQ & 0xFF00) >> 8;
  send_msg[4] = flush_enable; // flush control

  msg_index = 0; 

}

int tts_speech_content(char* text_to_speech, int done)
{
  int response = 0; 
  txt_len = strlen(text_to_speech); 

  for (int i = 0; i < txt_len; i++)
    { 
      send_msg[5 + msg_index] = text_to_speech[i]; 
      msg_index++; 
    }
  
  if (done) {

    send_msg[5 + msg_index] = '\0'; // null character

    msg_index += 6 ;
    send_msg[0] =  msg_index & 0xFF;          // LSB of msg len
    send_msg[1] = (msg_index & 0xFF00) >> 8; // MSB of msg len

    tts_send_message(send_msg, msg_index);

    response = tts_parse_response(ISC_TTS_SPEAK_RESP, 0x0000, 16); 

    WAIT_UNTIL_FINISHED; 
    
  }

  return response; 

}

int tts_speech_char(char text_to_speech, int done)
{
  int response = 0; 

  send_msg[5 + msg_index] = text_to_speech; 
  msg_index++; 

  if (done) {

    send_msg[5 + msg_index] = '\0'; // null character

    msg_index += 6 ;
    send_msg[0] =  msg_index & 0xFF;          // LSB of msg len
    send_msg[1] = (msg_index & 0xFF00) >> 8; // MSB of msg len

    tts_send_message(send_msg, msg_index);

    response = tts_parse_response(ISC_TTS_SPEAK_RESP, 0x0000, 16); 

    WAIT_UNTIL_FINISHED; 

  }

  return response; 

}

int tts_configure(LS_MODE mode)
{
  msg_len = 0x0C;

  send_msg[0] = msg_len & 0xFF;          // LSB of msg len
  send_msg[1] = (msg_len & 0xFF00) >> 8; // MSB of msg len
  send_msg[2] = ISC_TTS_CONFIG_REQ & 0xFF;
  send_msg[3] = (ISC_TTS_CONFIG_REQ & 0xFF00) >> 8;
  send_msg[4] = ISC_TTS_SAMPLE_RATE;
  send_msg[5] = VOICE-1; // we are starting with voice 1; 0 is needed for getter methods as termination token!
  send_msg[6] = (mode == LAMBDA_EPSON_M ? ISC_TTS_EPSON_PARSE : ISC_TTS_DECTALK_PARSE); 
  send_msg[7] = LANGUAGE;
  
  send_msg[8] = convert_speak_rate(SPEAK_RATE) % 256; 
  send_msg[9] = convert_speak_rate(SPEAK_RATE) / 256; 

  send_msg[10] = ISC_TTS_DATASOURCE;
  send_msg[11] = 0x00;

  tts_send_message(send_msg, msg_len);

  return tts_parse_response(ISC_TTS_CONFIG_RESP, 0x0000, 16); 
}


int tts_configure_dectalk(void) { 
  return tts_configure(LAMBDA_DECTALK_M); 
}

int tts_configure_epson(void) { 
  return tts_configure(LAMBDA_EPSON_M); 
}

int tts_configure_current(void) {  
  return tts_configure(CUR_MODE);  
}

int tts_set_volume(void) {
  char setvol_code[]={0x06, 0x00, 0x0A, 0x00, 0x00, 0x00};
  tts_send_message(setvol_code, 0x06);
  return tts_parse_response(ISC_AUDIO_VOLUME_RESP, 0x0000, 16); 
}

int tts_stop(void) {
  char setvol_code[]={0x06, 0x00, 0x18, 0x00, 0x00, 0x00};
  tts_send_message(setvol_code, 0x06);
  return tts_parse_response(ISC_TTS_STOP_RESP, 0x0000, 16); 
}

void tts_setup(void) {
   
  spi_begin_epson(); 
  tts_reset();

  int tmp = tts_get_version();
  check_for_error(tmp == 0x0402); 

  int success = tts_download();
  check_for_error(success);

  success = tts_boot_run();  
  check_for_error(success);

  _delay_ms(150); // Wait for the boot image to execute

  success = tts_registration();
  check_for_error(success);

  // Once again print version information 
  tts_get_version(); 

  success = tts_configure_audio();
  check_for_error(success);

  success = tts_set_volume();
  check_for_error(success); 

  success = tts_configure_epson();
  check_for_error(success);

#ifdef BOOTMESSAGE
  char mytext[] = "LambdaSpeak initialized.";

  success = tts_speech(mytext,0);
  check_for_error(success);
#endif

}

void speak(char message[]) {
  int success = tts_speech(message, 0);
  check_for_error(success);
 
}

void speak_progmem(PGM_P flash_address ) {
  int success = tts_speech_progmem(flash_address, 0);
  check_for_error(success); 
}

//
// LambdaSpeak 
// 

void init_allophones(void) {

  /* 00h PA1   PAUSE        6.4ms       20h /AW/  Out        254.8ms */
  // allo_length[0x00] = 64; 
  // allo_length[0x20] = 2548; 

  allo_map[0x00] = "_";
  allo_map[0x20] = "awt"; 

  /* 01h PA2   PAUSE       25.6ms       21h /DD2/ Do          72.1ms */
  // allo_length[0x01] = 256; 
  // allo_length[0x21] = 721; 

  allo_map[0x01] = "_"; 
  allo_map[0x21] = "d"; 

  /* 02h PA3   PAUSE       44.8ms       22h /GG3/ Wig        110.5ms */
  // allo_length[0x02] = 448; 
  // allo_length[0x22] = 1105; 

  allo_map[0x02] = "_"; 
  allo_map[0x22] = "g"; 

  /* 03h PA4   PAUSE       96.0ms       23h /VV/  Vest       127.4ms */
  // allo_length[0x03] = 960; 
  // allo_length[0x23] = 1274; 

  allo_map[0x03] = "_"; 
  allo_map[0x23] = "v"; 

  /* 04h PA5   PAUSE      198.4ms       24h /GG1/ Got         72.1ms */
  // allo_length[0x04] = 1984; 
  // allo_length[0x24] = 721; 

  allo_map[0x04] = "_"; 
  allo_map[0x24] = "g"; 

  /* 05h /OY/  Boy        291.2ms       25h /SH/  Ship       198.4ms */
  // allo_length[0x05] = 2912; 
  // allo_length[0x25] = 1984; 

  allo_map[0x05] = "oy"; 
  allo_map[0x25] = "sh"; 

  /* 06h /AY/  Sky        172.9ms       26h /ZH/  Azure      134.1ms */
  // allo_length[0x06] = 1729; 
  // allo_length[0x26] = 1341; 

  allo_map[0x06] = "ay"; 
  allo_map[0x26] = "zh"; 

  /* 07h /EH/  End         54.6ms       27h /RR2/ Brain       81.9ms */
  // allo_length[0x07] = 546; 
  // allo_length[0x27] = 819; 

  allo_map[0x07] = "eh"; 
  allo_map[0x27] = "rr"; 

  /* 08h /KK3/ Comb        76.8ms       28h /FF/  Food       108.8ms */
  // allo_length[0x08] = 768; 
  // allo_length[0x28] = 1088; 

  allo_map[0x08] = "k"; 
  allo_map[0x28] = "f";

  /* 09h /PP/  Pow        147.2ms       29h /KK2/ Sky        134.4ms */
  // allo_length[0x09] = 1472; 
  // allo_length[0x29] = 1344; 

  allo_map[0x09] = "p"; 
  allo_map[0x29] = "k"; 

  /* 0Ah /JH/  Dodge       98.4ms       2Ah /KK1/ Can't      115.2ms */
  // allo_length[0x0a] = 984; 
  // allo_length[0x2a] = 1152; 

  allo_map[0x0a] = "jh"; 
  allo_map[0x2a] = "k"; 

  /* 0Bh /NN1/ Thin       172.9ms       2Bh /ZZ/  Zoo        148.6ms */
  // allo_length[0x0b] = 1729; 
  // allo_length[0x2b] = 1486; 

  allo_map[0x0b] = "n"; 
  allo_map[0x2b] = "z"; 

  /* 0Ch /IH/  Sit         45.5ms       2Ch /NG/  Anchor     200.2ms */
  // allo_length[0x0c] = 455; 
  // allo_length[0x2c] = 2002; 

  allo_map[0x0c] = "ih"; 
  allo_map[0x2c] = "n"; 

  /* 0Dh /TT2/ To          96.0ms       2Dh /LL/  Lake        81.9ms */
  // allo_length[0x0d] = 960; 
  // allo_length[0x2d] = 819; 

  allo_map[0x0d] = "t"; 
  allo_map[0x2d] = "ll"; 

  /* 0Eh /RR1/ Rural      127.4ms       2Eh /WW/  Wool       145.6ms */
  // allo_length[0x0e] = 1274; 
  // allo_length[0x2e] = 1456; 

  allo_map[0x0e] = "r"; 
  allo_map[0x2e] = "w"; 

  /* 0Fh /AX/  Succeed     54.6ms       2Fh /XR/  Repair     245.7ms */
  // allo_length[0x0f] = 546; 
  // allo_length[0x2f] = 2457; 

  allo_map[0x0f] = "ax"; 
  allo_map[0x2f] = "ar"; 

  /* 10h /MM/  Milk       182.0ms       30h /WH/  Whig       145.2ms */
  // allo_length[0x10] = 1820; 
  // allo_length[0x30] = 1452; 

  allo_map[0x10] = "m"; 
  allo_map[0x30] = "w"; 

  /* 11h /TT1/ Part        76.8ms       31h /YY1/ Yes         91.0ms */
  // allo_length[0x11] = 768; 
  // allo_length[0x31] = 910; 

  allo_map[0x11] = "t"; 
  allo_map[0x31] = "yx"; 

  /* 12h /DH1/ They       136.5ms       32h /CH/  Church     147.2ms */
  // allo_length[0x12] = 1365; 
  // allo_length[0x32] = 1472; 

  allo_map[0x12] = "dh"; 
  allo_map[0x32] = "ch"; 

  /* 13h /IY/  See        172.9ms       33h /ER1/ Letter     109.2ms */
  // allo_length[0x13] = 1729; 
  // allo_length[0x33] = 1092; 

  allo_map[0x13] = "iy"; 
  allo_map[0x33] = "er"; 

  /* 14h /EY/  Beige      200.2ms       34h /ER2/ Fir        209.3ms */
  // allo_length[0x14] = 2002; 
  // allo_length[0x34] = 2093; 

  allo_map[0x14] = "ey"; 
  allo_map[0x34] = "er"; 

  /* 15h /DD1/ Could       45.5ms       35h /OW/  Beau       172.9ms */
  // allo_length[0x15] = 455; 
  // allo_length[0x35] = 1729; 

  allo_map[0x15] = "d"; 
  allo_map[0x35] = "ow"; 

  /* 16h /UW1/ To          63.7ms       36h /DH2/ Bath       182.0ms */
  // allo_length[0x16] = 637; 
  // allo_length[0x36] = 1820; 

  allo_map[0x16] = "uw"; 
  allo_map[0x36] = "dh"; 

  /* 17h /AO/  Aught       72.8ms       37h /SS/  Vest        64.0ms */
  // allo_length[0x17] = 728; 
  // allo_length[0x37] = 640; 

  allo_map[0x17] = "ao"; 
  allo_map[0x37] = "s"; 

  /* 18h /AA/  Hot         63.7ms       38h /NN2/ No         136.5ms */
  // allo_length[0x18] = 637; 
  // allo_length[0x38] = 1365; 

  allo_map[0x18] = "aa"; 
  allo_map[0x38] = "n"; 

  /* 19h /YY2/ Yes        127.4ms       39h /HH2/ Hoe        126.0ms */
  // allo_length[0x19] = 1274; 
  // allo_length[0x39] = 1260; 

  allo_map[0x19] = "yx"; 
  allo_map[0x39] = "hx"; 

  /* 1Ah /AE/  Hat         81.9ms       3Ah /OR/  Store      236.6ms */
  // allo_length[0x1a] = 819; 
  // allo_length[0x3a] = 2366; 

  allo_map[0x1a] = "ae"; 
  allo_map[0x3a] = "or"; 

  /* 1Bh /HH1/ He          89.6ms       3Bh /AR/  Alarm      200.2ms */
  // allo_length[0x1b] = 896; 
  // allo_length[0x3b] = 2002; 

  allo_map[0x1b] = "hx"; 
  allo_map[0x3b] = "ar"; 

  /* 1Ch /BB1/ Business    36.4ms       3Ch /YR/  Clear      245.7ms */
  // allo_length[0x1c] = 364; 
  // allo_length[0x3c] = 2457; 

  allo_map[0x1c] = "b"; 
  allo_map[0x3c] = "ehr"; 

  /* 1Dh /TH/  Thin       128.0ms       3Dh /GG2/ Guest       69.4ms */
  // allo_length[0x1d] = 1280; 
  // allo_length[0x3d] = 694; 

  allo_map[0x1d] = "th"; 
  allo_map[0x3d] = "g"; 

  /* 1Eh /UH/  Book        72.8ms       3Eh /EL/  Saddle     136.5ms */
  // allo_length[0x1e] = 728; 
  // allo_length[0x3e] = 1365; 

  allo_map[0x1e] = "uh"; 
  allo_map[0x3e] = "el"; 

  /* 1Fh /UW2/ Food       172.9ms       3Fh /BB2/ Business    50.2ms */
  // allo_length[0x1f] = 1729; 
  // allo_length[0x3f] = 502; 

  allo_map[0x1f] = "uw"; 
  allo_map[0x3f] = "b"; 

}

//
// Initialization
// 

void init_pins(void) {

  CONFIGURE_TO_CPC; 
  CONFIGURE_FROM_CPC; 
  CONFIGURE_TTS_INOUT; 
  CONFIGURE_LEDS; 
  CONFIGURE_RESET; 
  CONFIGURE_PCM; 

  return; 

}

//
//
//
 
void stop(void) {

  stop_timer; 

  TRANSMIT_ON;
  READY_ON; 

  return;

}

void cont0(void) {

  TRANSMIT_OFF;
  READY_ON; 

  length = 0; 

  return; 

}

void cont(void) {

  cont0(); 

  emulated_ssa1_buffer_size = 0; 
  
  start_timer; 
  emulated_ssa1_dktronics_ms_counter = 0; 

  if (CUR_MODE == SSA1_M) {
    speech_idle_loadme;
  } else if (CUR_MODE == DKTRONICS_M) {
    dk_speech_idle_loadme;
  }
  
  return;

}

void proceed(void) {

  if (CUR_MODE == SSA1_M || CUR_MODE == DKTRONICS_M )  
    cont(); 
  else 
    cont0();   
}

void speak_buffer(void) {

  // TODO - SSA1 VOICE AND RATE !!!!

  TRANSMIT_ON;
  READY_OFF; 

  if (CUR_MODE == SSA1_M || CUR_MODE == DKTRONICS_M) {

    stop();   

    tts_speech_start(0); 
    // ADD VOICE AND RATE HERE!!!
    sprintf(command_string, "[:phone arpa speak on][:rate %d][:n%d][", convert_speak_rate(SPEAK_RATE), VOICE-1);
    tts_speech_content(command_string, 0); 
 
    for (int i = 0; i < length; i++) {

      uint8_t byte = buffer[i]; 
      char*     allo = allo_map[byte]; 
      // uint16_t  dur = allo_length[byte] / 10; 

      if (byte < 5) { // "_" 
 	tts_speech_content(" ", 0); 
      } else {
	tts_speech_content(allo, 0); 
	tts_speech_content(" ", 0); 
      }      
    }

    sprintf(command_string, "][:n%d]", VOICE-1);
    tts_speech_content(command_string, 1); 

    /*
      sprintf(command_string, "%d", length);
      speak(command_string);  */ 
    
    if (CUR_MODE == SSA1_M || CUR_MODE == DKTRONICS_M) 
      cont(); 
    else 
      cont0(); 

  } else {

    // not SSA1 

    buffer[length-1] = 0; 
    speak( buffer); 

    cont0(); 
  
  }

  return;

}


void speak_dectalk(char* speak_buffer) {

  // TODO - SSA1 VOICE AND RATE !!!!

  TRANSMIT_ON;
  READY_OFF; 

  unsigned short txt_len = strlen(speak_buffer); 
 
  tts_speech_start(0); 
  // ADD VOICE AND RATE HERE!!!

  sprintf(command_string, "[:rate %d][:n%d]", convert_speak_rate(SPEAK_RATE), VOICE-1);
  tts_speech_content(command_string, 0); 
   
  for (int i = 0; i < txt_len; i++) {
    char c = speak_buffer[i]; 
    tts_speech_char(c, 0); 
  } 

  sprintf(command_string, "[:n%d]", VOICE-1);
  tts_speech_content(command_string, 1); 

  return;

}


//
//
// 


ISR(TIMER0_COMPA_vect) {  // timer0 overflow interrupt

  if (length > 0 ) {

    emulated_ssa1_dktronics_ms_counter++; 

    if (emulated_ssa1_dktronics_ms_counter == ssa_dktronics_loadme_again_after_ms) {

      if (CUR_MODE == SSA1_M) {
	speech_idle_loadme; 
	emulated_ssa1_buffer_size = 0; 
      } else if (CUR_MODE == DKTRONICS_M) 
	dk_speech_idle_loadme; 

    } else if ( emulated_ssa1_dktronics_ms_counter == ssa1_and_dk_flush_buffer_after_ms ) {

      emulated_ssa1_dktronics_ms_counter = 0;   
      speak_buffer(); 

    }
  }
}

//
//
// 


void command_confirm(char* message) {
  if (CONFIRM_COMMANDS) {
    speak(message);  
  }
} 

void speech_ready_message(void) {

  int confirm = CONFIRM_COMMANDS; 
  CONFIRM_COMMANDS = 1;   

  LEDS_ON; 

  switch (CUR_MODE) {
  case SERIAL_M : command_confirm("Serial mode."); break; 
  case SSA1_M : command_confirm("S-S-A 1 mode."); break; 
  case DKTRONICS_M : command_confirm("DeeKay Tronics mode."); break; 
  case LAMBDA_EPSON_M : command_confirm("Epson mode."); break; 
  case LAMBDA_DECTALK_M : command_confirm("DecTalk mode."); break; 
  default : break; 

  }

  LEDS_OFF; 
  CONFIRM_COMMANDS = confirm; 


  return; 

}

//
//
// 

void setup_pcm(void) {
 
  stop_timer;

  cli(); 

  //
  // configure COUNTER 0 
  // for OC0B = PCM_PIN = PB4 = PCM AUDIO OUTPUT
  // 

  OCR0B = 127; // start at 127 with PCM output

  OCR0A = 0xFE; // NECESSARY; FOR WHATEVER REASON! OTHERWISE NO PCM OUTPUT!
  DDRB |= ( 1 << PCM_PIN); // = OC0B = PCM_PIN PB4 = AMDRUM AUDIO OUTPUT
  
  TCCR0A |= (1 << COM0B1);  // USE TIMER 0, REGISTER B -> OCR0B 
  TCCR0A |= (1 << WGM00) ; // PHASE CORRECTED PWM, MODE 5: WGM00 = 1
  TCCR0B |= (1 << WGM02) | (1 << CS00); // NO CLOCK SCALING, MODE 5 WGM02 = 1

  TIMSK0 = 0; // NO INTERRUPTS ENABLED 

  //
  // configure TIMER2, 
  // 
  
  TCNT2 = 0x00; 

  // TCCR2B = (1 << CS20) | (1 << CS21) ; 
  TCCR2B = (1 << CS21) ;  // CLK FREQ / 8
  // TCCR2B = (1 << CS20) ;  // CLK FREQ 

  TIMSK2 = 0; // NO INTERRUPTS ENABLED 

  sei(); 

}

/* 
   void setup_pcm(int interrupts) {
 
   stop_timer;
   cli(); 

   //
   // configure COUNTER 0 
   // for OC0B = PCM_PIN = PB4 = PCM AUDIO OUTPUT
   // 

   OCR0B = 127; // start at 127 with PCM output

   OCR0A = 0xFE; // NECESSARY; FOR WHATEVER REASON! OTHERWISE NO PCM OUTPUT!
   DDRB |= ( 1 << PCM_PIN); // = OC0B = PCM_PIN PB4 = AMDRUM AUDIO OUTPUT
  
   TCCR0A |= (1 << COM0B1);  // USE TIMER 0, REGISTER B -> OCR0B 
   TCCR0A |= (1 << WGM00) ; // PHASE CORRECTED PWM, MODE 5: WGM00 = 1
   TCCR0B |= (1 << WGM02) | (1 << CS00); // NO CLOCK SCALING, MODE 5 WGM02 = 1

   //
   // configure TIMER2, 
   // 
  
   TCNT2 = 0x00; 

   if (interrupts == 0) {

   // TCCR2B = (1 << CS20) | (1 << CS21) ; 
   // TCCR2B = (1 << CS21) ;  // CLK FREQ / 8
   TCCR2B = (1 << CS20) ;  // CLK FREQ 

   } else {

   TCCR2B = (1 << CS20); 

   if ( interrupts == 1 ) {
   // use overflow flag 
   TIMSK2 |= (1 << TOIE2); // overflow interrupt counter 2 
   } else if ( interrupts == 2) {
   // use compare match mode
   setBit(TCCR2A, WGM21);  // Set the Timer Mode to CTC
   OCR2A = EEPROM_PCM_COMPA_VALUE ;   // Set the value that you want to count to
   setBit(TIMSK2, OCIE2A);  //Set the ISR COMPA vect
   }
   }

   }

*/

void pcm_test(void) {

  LEDS_ON;
  command_confirm("P C M test. Use reset button to quit."); 
  z80_run; 

  uint32_t sample = 0; 
  uint8_t byte = 0; 

  setup_pcm(); 

  while (1) {

    byte = pgm_read_byte(&pcm_samples[sample++]);
    OCR0B = byte; 

    byte = abs(byte - 127);

    if (byte < 8) {
      DATA_TO_CPC( 0); 
    } else if (byte < 16) {
      DATA_TO_CPC( 1); 
    } else if (byte < 32) {
      DATA_TO_CPC( 3); 
    } else if (byte < 40) {
      DATA_TO_CPC( 7); 
    } else if (byte < 48) {
      DATA_TO_CPC( 15); 
    } else if (byte < 64) {
      DATA_TO_CPC( 31); 
    } else if (byte < 80) {
      DATA_TO_CPC( 63); 
    } else if (byte < 104) {
      DATA_TO_CPC( 127); 
    } else 
      DATA_TO_CPC( 255); 
    
    if(sample == pcm_length) { 
      sample=0; 
      DATA_TO_CPC( 0); 
      _delay_ms(1000); 
    } 

    _delay_us(120);

  }
 
}


void amdrum_mode_hq(void) {

  LEDS_ON;

  CUR_MODE = AMDRUM_M; 
  command_confirm("H q amdrum mode. Power cycle to quit.");  

  z80_run; 
  
  AMDRUM_ON; 

  setup_pcm();   
  cli();
  
  uint8_t last_databus = 0; 
  
  while(1) {

    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
    DATA_FROM_CPC(databus); 

    if ( last_databus != databus) {
      last_databus = databus; 
      databus = databus > 250 ? 250 : databus; 
      databus = databus < 5 ? 5 : databus; 
      OCR0B = databus;   
    }
  }
 
} 

void amdrum_mode(void) {

  LEDS_ON;

  CUR_MODE = AMDRUM_M; 
  command_confirm("Standard amdrum mode. Send 4 6 4 6 1 2 8 to quit.");  

  z80_run; 
  
  AMDRUM_ON; 

  setup_pcm();   
  cli();
  
  uint8_t clipped_databus = 0; 
  uint8_t last_databus = 0; 
  uint8_t byte = 0; 
  uint8_t exit_counter = 0; 
  
  while(1) {
    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
    DATA_FROM_CPC(databus); 

    if ( last_databus != databus) {

      last_databus = databus; 

      clipped_databus = databus > 250 ? 250 : databus; 
      clipped_databus = clipped_databus < 5 ? 5 : clipped_databus; 
      OCR0B = clipped_databus; 

      byte = abs(databus - 127);

      if (byte < 8) {

	// exit after 4 6 4 6 1 2 8 
	//            0 1 2 3 4 5 6 

	if ( (databus == 128 && ( exit_counter == 4)) || 
	     (databus == 129 && ( exit_counter == 5)) || 
	     (databus == 131 && ( exit_counter == 0 || exit_counter == 2)) ||
	     (databus == 133 && ( exit_counter == 1 || exit_counter == 3))) 
	  exit_counter++; 	
	else 
	  exit_counter = 0; 

	DATA_TO_CPC( 0);       
    
      } else if (byte < 16) {
	
	if (databus == 135 && ( exit_counter == 6)) {
	  	  
	  process_reset(); 
	  	  
	}

	DATA_TO_CPC( 1); 

	exit_counter = 0;     

      } else { 

	exit_counter = 0;     

	if (byte < 32) {
	  DATA_TO_CPC( 3); 
	} else if (byte < 40) {
	  DATA_TO_CPC( 7); 
	} else if (byte < 48) {
	  DATA_TO_CPC( 15); 
	} else if (byte < 64) {
	  DATA_TO_CPC( 31); 
	} else if (byte < 80) {
	  DATA_TO_CPC( 63); 
	} else if (byte < 104) {
	  DATA_TO_CPC( 127); 
	} else 
	  DATA_TO_CPC( 255); 
      }
    }
  }
 
} 

void amdrum_mode_custom(void) {

  LEDS_ON;

  CUR_MODE = AMDRUM_M; 
  command_confirm("Custom amdrum mode. Enable lightshow?");  

  z80_run; 

  //
  //  Lightshow on / off? 
  //

  LEDS_ON;
  speech_native_ready; 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  _delay_us(3);
  DATA_FROM_CPC(databus); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  uint8_t lightshow = databus; 

  speech_native_busy; 
  LEDS_OFF;

  //
  //  Exit command enabled 
  //

  command_confirm("Enable exit check?");  

  LEDS_ON;
  speech_native_ready; 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  _delay_us(3);
  DATA_FROM_CPC(databus); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  uint8_t exitcheck = databus; 

  speech_native_busy; 
  LEDS_OFF;

  //
  //  Clip range
  //

  command_confirm("Send clip range.");  

  LEDS_ON;
  speech_native_ready; 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  _delay_us(3);
  DATA_FROM_CPC(databus); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  uint8_t cliprange = databus;  

  speech_native_busy; 
  LEDS_OFF;


  //
  // Dynamic Smoothing
  // 

  command_confirm("Send smoothing delta.");  

  LEDS_ON;
  speech_native_ready; 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  _delay_us(3);
  DATA_FROM_CPC(databus); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  uint8_t smoothing = databus;  

  speech_native_busy; 
  LEDS_OFF;


  //
  //
  //
  
  AMDRUM_ON; 

  setup_pcm();   
  cli();
  
  uint8_t last_databus = 0; 
  uint8_t clipped_databus = 0; 
  uint8_t byte = 0; 
  uint8_t exit_counter = 0; 

  uint8_t clip_max = 255 - cliprange; 
  uint8_t clip_min = cliprange; 
  
  while(1) {

    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
    DATA_FROM_CPC(databus); 

    if ( abs(last_databus - databus) > smoothing ) {

      last_databus = databus; 

      clipped_databus = databus > clip_max ? clip_max : databus; 
      clipped_databus = clipped_databus < clip_min ? clip_min : clipped_databus; 
      OCR0B = clipped_databus; 

      if (exitcheck || lightshow) {

	byte = abs(databus - 127);

	if (byte < 8) {

	  // exit after 4 6 4 6 1 2 8 
	  //            0 1 2 3 4 5 6 

	  if (exitcheck) {
	    if ( (databus == 128 && ( exit_counter == 4)) || 
		 (databus == 129 && ( exit_counter == 5)) || 
		 (databus == 131 && ( exit_counter == 0 || exit_counter == 2)) ||
		 (databus == 133 && ( exit_counter == 1 || exit_counter == 3))) 
	      exit_counter++; 	
	    else 
	      exit_counter = 0; 
	  }

	  if (lightshow) 
	    DATA_TO_CPC( 0);       
    
	} else if (byte < 16) {
	
	  if (exitcheck && ( databus == 135 ) && ( exit_counter == 6)) {
	  	  
	    process_reset(); 
	  	  
	  }

	  if (lightshow) 
	    DATA_TO_CPC( 1); 

	  exit_counter = 0;     

	} else { 

	  if (exitcheck) 
	    exit_counter = 0;     

	  if (lightshow)  {
	    if (byte < 32) {
	      DATA_TO_CPC( 3); 
	    } else if (byte < 40) {
	      DATA_TO_CPC( 7); 
	    } else if (byte < 48) {
	      DATA_TO_CPC( 15); 
	    } else if (byte < 64) {
	      DATA_TO_CPC( 31); 
	    } else if (byte < 80) {
	      DATA_TO_CPC( 63); 
	    } else if (byte < 104) {
	      DATA_TO_CPC( 127); 
	    } else 
	      DATA_TO_CPC( 255); 
	  }
	}
      }
    }
  }
 
} 

//
// RTC 
// 

void twi_init(void) {
  TWSR=0x00;
  TWBR=0x0F;//see how to configure clock freq of twi.here the freq for 16mhz xtal is nearabout 347khz
  TWCR =(1<<TWEN);//enable TWEN bit 
}

void twi_start(void) {
  TWCR=(1<<TWINT)|(1<<TWSTA)|(1<<TWEN);//TWINT=0 by writting one to it,TWSTA bit=1,TWEN bit=1 to transmit start condition when the bus is free.
  while((TWCR & (1<<TWINT))==0);//stay here and poll until TWINT becomes 1 at the end of transmit.
}

void twi_stop(void) {
  TWCR=(1<<TWINT)|(1<<TWSTO)|(1<<TWEN);//TWINT=0 by writting one to it,TWSTO bit=1,TWEN bit=1 to transmit stOP condition.
}

void twi_send(uint8_t data) {
  TWDR=data;//send the data to TWDR resistor
  TWCR=(1<<TWINT)|(1<<TWEN);//TWINT=0 by writting one to it,TWEN bit=1 to enable twi module
  while((TWCR & (1<<TWINT))==0);//stay here and poll until TWINT becomes 1 at the end of transmit.
}

uint8_t twi_receive(uint8_t ack_val) {
  TWCR=(1<<TWINT)|(1<<TWEN)|(ack_val<<TWEA);//if we want to receive more than one byte,we will send 1 as ack_val to send an acknowledge.At the time of last by te,we will send 0 as ack_val to send NACK.
  while((TWCR & (1<<TWINT))==0);//stay here and poll until TWINT becomes 1 at the end of transmit.
  return TWDR;
}

//
//
//


uint8_t BCDtoDEC(uint8_t bcd_val) {
return (((bcd_val >> 4) * 10) + (bcd_val & 0x0F));
}

uint8_t DECtoBCD(uint8_t dec_val) {
  return (((dec_val / 10 ) * 16) + (dec_val % 10));
}

void ds3231_init(void){
  twi_init();
  twi_start();
  twi_send(0xD0);//initial address of ds3231+0 to write next byte
  twi_send(0x0E);//write location of control resistor
  twi_send(0x00);//Enable SQW at 1hz.This is to make atmega sync with DS3231 every second
  twi_stop();
} 

void ds3231_set_24(void) {
 
  rtc_hours &= 0b01111111;//clear first two bits.

  twi_start();
  twi_send(0xD0);//initial address of ds3231+0 to write next byte
  twi_send(0x00);//select location 0 to update 
  twi_send(rtc_seconds);//update second
  twi_send(rtc_minutes);//write location of control resistor
  twi_send(rtc_hours);//write 0 to the content of the resistor
  twi_send(rtc_weekday);//update day of the week
  twi_send(rtc_date);//update date
  twi_send(rtc_month);//update month
  twi_send(rtc_year);//update year
  twi_stop();
}

void ds3231_get(void) {

  twi_start();
  twi_send(0xD0);
  twi_send(0x00);
  
  twi_start();
  twi_send(0xD1);//ds3231 address+1 to read

  rtc_seconds = twi_receive(1);//1 to send ACK
  rtc_minutes = twi_receive(1);//1 to send ACK
  rtc_hours = twi_receive(1);//0 to send NACK

  rtc_hours &= 0b01111111; 

  rtc_weekday = twi_receive(1);//1 to send ACK,read day
  rtc_date = twi_receive(1);//1 to send ACK,read date
  rtc_month = twi_receive(1);//1 to send ACK,read month
  rtc_year = twi_receive(0);//0 to send NACK,read year

  twi_stop();

  twi_start();
  twi_send(0xD0);
  twi_send(0x11);

  twi_start();
  twi_send(0xD1);//ds3231 address+1 to read
   
  rtc_msb_temp = twi_receive(0);

  twi_stop();

  rtc_hours_dec = BCDtoDEC(rtc_hours);
  rtc_mins_dec = BCDtoDEC(rtc_minutes);
  rtc_secs_dec = BCDtoDEC(rtc_seconds);

  rtc_year_dec = BCDtoDEC(rtc_year);
  rtc_month_dec = BCDtoDEC(rtc_month);
  rtc_date_dec = BCDtoDEC(rtc_date);
  rtc_weekday_dec = BCDtoDEC(rtc_weekday);


}


void i2c_get_time_or_date(uint8_t get_date) { 

  uint8_t old_confirm = CONFIRM_COMMANDS; 
  CONFIRM_COMMANDS = 1; 

  ds3231_init(); 
  ds3231_get(); 

  if (! get_date) { 

    sprintf(command_string, "It is %d o clock, %d minutes, and %d seconds.", rtc_hours_dec, rtc_mins_dec, rtc_secs_dec);   

  } else {

    char* month_s = 0; 
    char* weekday_s = 0; 

    switch ( rtc_weekday_dec ) {

    case 1 : weekday_s = "Monday"; break; 
    case 2 : weekday_s = "Tuesday"; break; 
    case 3 : weekday_s = "Wednesday"; break; 
    case 4 : weekday_s = "Thursday"; break; 
    case 5 : weekday_s = "Friday"; break; 
    case 6 : weekday_s = "Saturday"; break; 
    case 7 : weekday_s = "Sunday"; break; 
    default : weekday_s = "Unknown"; break; 

    }

    switch ( rtc_month_dec ) {

    case 1 : month_s = "January"; break; 
    case 2 : month_s = "February"; break; 
    case 3 : month_s = "March"; break; 
    case 4 : month_s = "April"; break; 
    case 5 : month_s = "May"; break; 
    case 6 : month_s = "June"; break; 
    case 7 : month_s = "July"; break; 
    case 8 : month_s = "August"; break; 
    case 9 : month_s = "September"; break; 
    case 10 : month_s = "October"; break; 
    case 11 : month_s = "November"; break; 
    case 12 : month_s = "December"; break; 
    default : month_s = "Unknown"; break; 

    }

    sprintf(command_string, "It is %s, %s %d 20%d.", weekday_s, month_s, rtc_date_dec, rtc_year_dec); 

  }
  command_confirm(command_string); 

  CONFIGURE_LEDS; 
  LEDS_OFF; 

  DATA_TO_CPC(0); 

  CONFIRM_COMMANDS = old_confirm;

  return;

}

void i2c_speak_temp(void) { 

  uint8_t old_confirm = CONFIRM_COMMANDS; 
  CONFIRM_COMMANDS = 1; 

  ds3231_init(); 
  ds3231_get(); 

  sprintf(command_string, "Temperature is %d degrees celsius.", rtc_msb_temp); 
  command_confirm(command_string); 

  CONFIGURE_LEDS; 
  LEDS_OFF; 

  CONFIRM_COMMANDS = old_confirm;
  
  return;

}

void i2c_speak_time(void) { 
  i2c_get_time_or_date(0); 
}

void i2c_speak_date(void) { 
  i2c_get_time_or_date(1); 
}

uint8_t read_from_cpc_bcd(void) {

  uint8_t data; 
  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  _delay_us(3);
  DATA_FROM_CPC(data); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 
  LEDS_OFF;
  return DECtoBCD(data); 
}

void cpc_input_8bit(uint8_t byte) { 

  DATA_TO_CPC(byte); 
  z80_run; 
  CPC_READ_DELAY;
  DATA_TO_CPC(255); 
  CPC_READ_DELAY;  
  DATA_TO_CPC(0); 
}

void i2c_set_time(void) { 

  ds3231_init(); 
  ds3231_get(); 

  z80_run; 

  LAMBDA_EPSON_ON; 

  speech_native_busy;  
  command_confirm("Set time. Send hours.");  

  LEDS_ON;
  speech_native_ready;  

  rtc_hours = read_from_cpc_bcd(); 

  speech_native_busy;  
  command_confirm("Send minutes.");   

  LEDS_ON;
  speech_native_ready;  
  
  rtc_minutes = read_from_cpc_bcd(); 

  speech_native_busy;  
  command_confirm("Send seconds.");  

  LEDS_ON;
  speech_native_ready;  

  rtc_seconds = read_from_cpc_bcd(); 

  ds3231_set_24(); 

  command_confirm("Time set"); 

  CONFIGURE_LEDS; 
  LEDS_OFF; 

  DATA_TO_CPC(0); 

  return; 

}


void i2c_set_date(void) { 

  ds3231_init(); 
  ds3231_get(); 

  z80_run; 

  LAMBDA_EPSON_ON; 

  speech_native_busy;  
  command_confirm("Set date. Send year.");  

  LEDS_ON;
  speech_native_ready;  

  rtc_year = read_from_cpc_bcd(); 

  speech_native_busy;  
  command_confirm("Send month.");  

  LEDS_ON;
  speech_native_ready;  
  rtc_month = read_from_cpc_bcd(); 

  speech_native_busy;  
  command_confirm("Send date.");  

  LEDS_ON;
  speech_native_ready;  

  rtc_date = read_from_cpc_bcd(); 

  speech_native_busy;  
  command_confirm("Send weekday.");  

  LEDS_ON;
  speech_native_ready;  

  rtc_weekday = read_from_cpc_bcd(); 

  ds3231_set_24(); 

  command_confirm("Date set"); 

  CONFIGURE_LEDS; 
  LEDS_OFF; 

  DATA_TO_CPC(0); 

  return; 

}

void i2c_get_clock_reg(uint8_t kind) {

  ds3231_init(); 
  // _delay_us(00); 
  ds3231_get(); 

  switch ( kind ) {
  case 0 : cpc_input_8bit(rtc_hours_dec); break; 
  case 1 : cpc_input_8bit(rtc_mins_dec); break; 
  case 2 : cpc_input_8bit(rtc_secs_dec); break; 
  case 3 : cpc_input_8bit(rtc_year_dec); break; 
  case 4 : cpc_input_8bit(rtc_month_dec); break; 
  case 5 : cpc_input_8bit(rtc_date_dec); break; 
  case 6 : cpc_input_8bit(rtc_weekday_dec); break;        
  }

  return; 

}

void i2c_get_temp(void) {

  ds3231_init(); 
  ds3231_get(); 
 
  cpc_input_8bit(rtc_msb_temp); 
  
}

//
//
// 

void native_mode_epson(void) {
  CUR_MODE = LAMBDA_EPSON_M; 
  command_confirm("Native Epson mode."); 
  tts_configure_epson();
}

void ssa1_mode(void) {
  CUR_MODE = SSA1_M; 
  command_confirm("S-S-A 1 mode."); 
  tts_configure_dectalk();  
}

void dktronics_mode(void) {
  CUR_MODE = DKTRONICS_M; 
  command_confirm("DeeKay Tronics mode."); 
  tts_configure_dectalk();  
}

void native_mode_dectalk(void) {
  CUR_MODE = LAMBDA_DECTALK_M; 
  command_confirm("Native DecTalk mode."); 
  tts_configure_dectalk();
}

void non_blocking(void) {
  BLOCKING = 0; 
  command_confirm("Non-Blocking mode."); 
}

void non_blocking_confirmations(void) {
  command_confirm("Use non-blocking confirmations."); 
  NON_BLOCK_CONFIRMATIONS = 1; 
}

void blocking_confirmations(void) {
  NON_BLOCK_CONFIRMATIONS = 0; 
  command_confirm("Use blocking confirmations."); 
}

void blocking(void) {
  BLOCKING = 1; 
  command_confirm("Blocking mode."); 
}

void confirmations_on(void) {
  CONFIRM_COMMANDS = 1; 
  command_confirm("Confirmations on."); 
}

void confirmations_off(void) {
  //confirm_command("Confirmations off."); 
  CONFIRM_COMMANDS = 0;   
}

void set_voice(uint8_t voice) {  
  VOICE = voice; 
  tts_configure_current(); 

  sprintf(command_string, "Voice set to %d.", voice); 
  command_confirm(command_string); 
}

void set_voice_default(void) {    
  set_voice(ISC_TTS_VOICE+1);   
}

void set_volume(uint8_t volume) {  
  VOLUME = volume; 
  tts_configure_audio();

  sprintf(command_string, "Volume set to %d.", volume); 
  command_confirm(command_string); 
} 

void set_volume_default(void) {    
  set_volume(VOLUME_DEFAULT);   
}

void set_rate(uint8_t rate) {  
  SPEAK_RATE = rate; 
  tts_configure_current();

  sprintf(command_string, "Speech rate set to %d.", rate); 
  command_confirm(command_string); 
} 

void set_rate_default(void) {    
  set_rate(SPEAK_RATE_DEFAULT);   
}

void announce_cur_mode(void) {  
  speech_ready_message();
}

void set_buffer_delay(uint8_t del) {

  FLUSH_BUFFER_DELAY = del; 
  ssa1_and_dk_flush_buffer_after_ms = 10 + FLUSH_BUFFER_DELAY * 10;
  // default ms = 110 => del = 10 

  sprintf(command_string, "Flush buffer after %d milliseconds.", 
	  ssa1_and_dk_flush_buffer_after_ms); 

  command_confirm(command_string); 
} 

void set_buffer_delay_default(void) {
  set_buffer_delay(FLUSH_BUFFER_DELAY_DEFAULT); 
}

/*
void stop_command(void) {
  tts_stop(); 
}
*/

void flush_command(void) {
  buffer[length++] = 0; 
  speak_buffer(); 
}

/*
  void native_wait_before_return(void) {
  
  WAIT_BEFORE_RETURN = 1; 
  command_confirm("Native mode wait before return."); 

  } 


  void native_wait_before_send(void) {
  
  WAIT_BEFORE_RETURN = 0; 
  command_confirm("Native mode wait before send."); 

  } 
*/ 

void english(void) {

  LANGUAGE = 0; 
  command_confirm("English mode."); 
  tts_configure_current();

}

void spanish(void) {

  LANGUAGE = 1; 
  command_confirm("Castilian spanish mode."); 
  tts_configure_current();

}

void fast_getters(void) {

  FAST_CPC_GETTERS = 1; 
  command_confirm("Fast getters."); 

}

void medium_getters(void) {

  FAST_CPC_GETTERS = 2; 
  command_confirm("Medium getters."); 

}

void slow_getters(void) {

  FAST_CPC_GETTERS = 0; 
  command_confirm("Slow getters."); 

}

void handshake_getters(void) {

  FAST_CPC_GETTERS = 3; 
  command_confirm("Handshake getters."); 

}

//
//
// 

void use_std_amdrum(void) {

  CUR_AMDRUM_MODE = AMDRUM_STD; ; 
  command_confirm("Use standard amdrum mode."); 

}

void use_hq_amdrum(void) {

  CUR_AMDRUM_MODE = AMDRUM_HQ; ; 
  command_confirm("Use h q Amdrum mode."); 

}

void use_custom_amdrum(void) {

  CUR_AMDRUM_MODE = AMDRUM_CUSTOM; ; 
  command_confirm("Use custom amdrum mode."); 

}

//
//
//


void cpc_input(char* message, uint8_t nibble, uint8_t four_bit) {

  if (four_bit) {
    nibble = nibble << 4; // upper nibble 7, 6, 5, 4 
  }

  z80_run;   
  DATA_TO_CPC(nibble); 
  CPC_READ_DELAY; 

  if (CONFIRM_COMMANDS) {

  sprintf(command_string, "%s %d.", message, four_bit ? nibble >> 4 : nibble); 
  
    command_confirm(command_string); 

    // this zero is needed so the CPC knows that it has read the value!
    // otherwise I can't write a busy waiting loop on the cpc 
    // it also has to wait until speech confirm has finished! 
    // we can't return earlier from that command 

    DATA_TO_CPC(0); 
    CPC_READ_DELAY; 

  } else {

    DATA_TO_CPC(0);     
    CPC_READ_DELAY; 

  }

  // CUR_MODE = START_OVER_SAME_MODE; 
  return; 

}

void get_mode(void) {
  
  uint8_t bout  = CUR_MODE;  // 0, 1, 2, 3

  bout |= BLOCKING << 2;  // | 4
  bout |= LANGUAGE << 3;  // | 8 

  cpc_input("Current mode ", bout, 1); // 4 bit 

}

void get_full_mode(void) {
  
  uint8_t bout  = CUR_MODE;  // 0  - 9 : 4 bits 

  bout |= BLOCKING << 4;  
  bout |= NON_BLOCK_CONFIRMATIONS << 5;  
  bout |= LANGUAGE << 6;   
  bout |= CONFIRM_COMMANDS << 7; 

  cpc_input("Current full mode ", bout, 0);  // 8 bit 

}

void get_volume(void) {

  cpc_input("Current volume ", VOLUME, 1); 

}

void get_rate(void) {

  cpc_input("Current speak rate ", SPEAK_RATE, 1); 

}

void get_voice(void) {

  cpc_input("Current voice ", VOICE, 1); 

}

void get_language(void) {

  cpc_input("Current language ", LANGUAGE, 1); 

}

void get_delay(void) {

  cpc_input("Current flush delay ", FLUSH_BUFFER_DELAY, 1); 

}

void get_version(void) {

  cpc_input("Current version ", VERSION, 0);  // 8 bit!

}

//
//
//

void speak_progmem_string_from(PGM_P flash_address) {
  tts_configure_epson(); 
  speak_progmem(flash_address);  
  tts_configure_current(); 

}

void speak_copyright_note(void) {

  uint8_t voice = VOICE; 
  uint8_t rate = SPEAK_RATE; 
  uint8_t language = LANGUAGE; 
  uint8_t volume = VOLUME; 
   
  VOICE = ISC_TTS_VOICE + 1; 
  SPEAK_RATE = SPEAK_RATE_DEFAULT; 
  LANGUAGE = ISC_TTS_LANGUAGE;  
  VOLUME = VOLUME_DEFAULT; 

  speak_progmem_string_from((PGM_P) &COPYRIGHT);  

  VOICE = voice;
  SPEAK_RATE = rate; 
  LANGUAGE = language; 
  VOLUME = volume; 

  tts_configure_current();  

}


static volatile uint8_t hal_quote = 0; 

void speak_hal9000_quote(void) {
  speak_progmem_string_from((PGM_P) pgm_read_word(&(HAL[hal_quote])));  
  hal_quote = (hal_quote + 1) % num_quotes; 

}

void sing_daisy(void) {

  tts_configure_dectalk(); 

  tts_speech_start(0); 

  tts_speech_content("[:phone arpa speak on][:rate 200][:n0][dey<650,22>ziy<600,19> dey<650,15>ziy<600,10>gih<200,12>v miy<200,14> yurr<200,15> ae<400,12>nsax<200,15>r duw<750,10> _<400,10>]\n", 1); 

  tts_speech_start(0); 
  tts_speech_content("[:phone arpa speak on][:rate 200][:n0][ay<600,17>m hxae<500,22>f kr ey<650,19>ziy<600,15> ao<200,12>ll fao<200,14>r dhax<200,15> llah<400,17>v ao<200,19>v yu<750,17> _<400,17>]\n", 1); 

  tts_speech_start(0); 
  tts_speech_content("[:phone arpa speak on][:rate 200][:n0][ih<200,19>t wow<200,20>nt biy<200,19> ax<200,17> stay<500,22>llih<200,19>sh mae<350,17>rih<400,15>jh<150,15>]\n", 1); 

  tts_speech_start(0); 
  tts_speech_content("[:phone arpa speak on][:rate 200][:n0][ay<200,17> kae<400,19>nt ax<200,15>fow<400,12>rd ax<200,15> kae<350,12>rih<400,10>jh<150,10>]\n", 1); 

  tts_speech_start(0); 
  tts_speech_content("[:phone arpa speak on][:rate 200][:n0][bah<200,10>t yu<500,15>d lluh<200,19>k swiy<400,17>t ah<200,10>p ao<500,15>n dhax<200,19> siy<200,17>t ao<200,17>v ah<200,19> bay<200,22>six<200,19>kel<200,15> bih<400,17>llt fao<200,10>r tuw<800,15>]\n", 1); 

  tts_configure_current();
  
}


void echo_test_program(void) {
  
  command_confirm("Running echo test port program. Use reset button to quit."); 
  
  z80_run;

  while (1) {

    LEDS_ON;

    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
    loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

    LEDS_OFF; 

    DATA_FROM_CPC(databus); 
    DATA_TO_CPC(databus); 
     
  }  
}

void echo_program(void) {
  
  command_confirm("Running echo program. Send 00 to quit, 0 x for 0."); 
  
  DATA_TO_CPC(0); 
  z80_run;

  while (1) {

    LEDS_ON;

    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
    loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

    LEDS_OFF; 

    DATA_FROM_CPC(databus); 

    if (! databus) { // 0 read? 
      LEDS_ON;
      loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
      loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 
      LEDS_OFF; 
      DATA_FROM_CPC(databus); 
      if (! databus) // 00 read? quit
	return; 
      else
	databus = 0; 
    }

    DATA_TO_CPC(databus); 
     
  }  
}



void wait_and_echo_single_byte(void) {

  DATA_TO_CPC(0); 

  z80_run;
  LEDS_ON;

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  speech_native_ready;  
  DATA_FROM_CPC(databus); 
  DATA_TO_CPC(databus); 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  LEDS_OFF; 
  DATA_TO_CPC(0); 
  speech_native_ready;  

}


void echo_byte(void) {
  
  speech_native_busy;  
  command_confirm("Echo byte. Waiting for byte."); 

  z80_run;

  LEDS_ON;

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  LEDS_OFF; 

  speech_native_ready;  
  DATA_FROM_CPC(databus); 
  DATA_TO_CPC(databus); 

  // don't do that, voids the echo byte on the databus! 
  // speech_native_busy;  
  command_confirm("Waiting for port release."); 

  loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

  LEDS_OFF; 
  DATA_TO_CPC(0); 
  speech_native_ready;  
     
}

/*
void echo_test_program_dk(void) {
  
  command_confirm("Running echo test port program 2. Use reset button to quit."); 
  
  z80_run;

  while (1) {

    LEDS_ON;

    loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE_DK); 
    loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE_DK); 

    LEDS_OFF; 

    DATA_FROM_CPC(databus); 
    DATA_TO_CPC(databus); 
     
  }  
}
*/

//
// Serial / UART 
//

#define READ_ARGUMENT_FROM_DATABUS(databus) { serial_ready; loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); serial_busy; LAMBDA_EPSON_ON; loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); delay_us(3); DATA_FROM_CPC(databus); SERIAL_ON; }

// DO NOT SEND TO 0! This has the potential to be confused with NO BYTE AVAILABLE in Serial Mode Command 255, 7 
// #define SEND_TO_CPC_DATABUS(byte) { DATA_TO_CPC( ( byte ) ); CPC_READ_DELAY; DATA_TO_CPC(0); CPC_READ_DELAY; }

// new: use handshake 
//#define SEND_TO_CPC_DATABUS(byte) { DATA_TO_CPC( ( byte ) ); loop_until_write_happened; }

#define SEND_TO_CPC_DATABUS(byte) { cli();  DATA_TO_CPC( ( byte ) ); sei();  CPC_READ_DELAY; }
 
// 
//
// 
 
void usart_on0(uint8_t rate, uint8_t width, uint8_t parity, uint8_t stop_bits) {

  SERIAL_BAUDRATE = rate; 
  SERIAL_WIDTH = width; 
  SERIAL_PARITY = parity; 
  SERIAL_STOP_BITS = stop_bits; 
 
  UBRR0H = 0;    
  switch (rate) {
  case 0 : UBRR0H = ( 416 >> 8) & 0xFF; UBRR0L = 416 & 0xFF; SERIAL_RATE = 2400; break; // 2400 
  case 1 : UBRR0L = 207; SERIAL_RATE = 4800; break; // 4800 
  case 2 : UBRR0L = 103; SERIAL_RATE = 9600; break; // 9600 
  case 3 : UBRR0L = 68; SERIAL_RATE = 14400; break;  // 14400 
  case 4 : UBRR0L = 51; SERIAL_RATE = 19200; break;  // 19200
  case 5 : UBRR0L = 34; SERIAL_RATE = 28800; break;  // 28800
  case 6 : UBRR0L = 31; SERIAL_RATE = 31250; break;  // 31250 MIDI ! NEW
  case 7 : UBRR0L = 25; SERIAL_RATE = 38400; break;  // 38400 
  case 8 : UBRR0L = 16; SERIAL_RATE = 57600; break;  // 57600 
  case 9 : UBRR0L = 12; SERIAL_RATE = 76800; break;  // 76800 
  case 10 : UBRR0L = 8; SERIAL_RATE = 115200; break;  // 115200 

  case 11 : UBRR0H = ( 3332 >> 8) & 0xFF; UBRR0L = 3332 & 0xFF; SERIAL_RATE = 300; break;  // 300
  case 12 : UBRR0H = ( 1666 >> 8) & 0xFF; UBRR0L = 1666 & 0xFF; SERIAL_RATE = 600; break;  // 600 
  case 13 : UBRR0H = ( 1110 >> 8) & 0xFF; UBRR0L = 1110 & 0xFF; SERIAL_RATE = 900; break;  // 900 
  case 14 : UBRR0H = ( 832 >> 8) & 0xFF; UBRR0L = 832 & 0xFF; SERIAL_RATE = 1200; break;  // 1200 
  
  default :  UBRR0L = 103; SERIAL_RATE = 9600; // 9600 
  }

  UCSR0C = 0; 

  switch (parity) { 
  case 0 :                                         break; // no parity 
  case 1 : UCSR0C |= (1 << UPM01) | (1 << UPM00) ; break; // odd parity 
  case 2 : UCSR0C |= (1 << UPM01)                ; break; // even parity 
  default : break; 
  }

  switch (stop_bits) { 
  case 2 :                         break; // 1 stop bit
  case 1 : UCSR0C |= (1 << USBS0); break; // 2 stop bit
  default : break; 
  }


  switch (width) {
  case 8 : UCSR0C |= (1 << UCSZ00) | (1 << UCSZ01); break; // 8bit 
  case 7 : UCSR0C |=                 (1 << UCSZ01); break; // 7bit 
  case 6 : UCSR0C |= (1 << UCSZ00)                ; break; // 6bit 
  case 5 :                                          break; // 5bit 

  default : UCSR0C |= (1 << UCSZ00) | (1 << UCSZ01);       // 8bit  

  }

  UCSR0B = 0; 
  
}

//
//
//

void usart_off(void) {    
  UCSR0B = 0;
}

void usart_init(void) {
  usart_off(); 
  usart_on0(SERIAL_BAUDRATE, SERIAL_WIDTH, SERIAL_PARITY, SERIAL_STOP_BITS); 

  UCSR0B |= (1 << RXEN0) | (1 << RXCIE0) | (1 << TXEN0); 

}

//
// RX ISR Receive / Buffered
//

ISR(USART0_RX_vect) {  
  z80_halt; 
  
  SERIAL_BYTE_RECEIVED = UDR0; 


      if (! ( (( cpc_read_cursor == 0 ) && ( usart_input_buffer_index == ( SPEECH_BUFFER_SIZE - 1)))
	      ||  
	      ( cpc_read_cursor == ( usart_input_buffer_index + 1)))) { 

	if (usart_input_buffer_index < SPEECH_BUFFER_SIZE) {
	  buffer[usart_input_buffer_index] = SERIAL_BYTE_RECEIVED; 

	  // a byte was put into the buffer
	  usart_input_buffer_index++;    
	  bytes_available++;     

	  if (usart_input_buffer_index == SPEECH_BUFFER_SIZE) {
	  // wrap around, buffer full 
	  usart_input_buffer_index = 0;       	  
	  }
	}
      }
  z80_run; 

}

 
//
// TX Transmit
// 


void USART_Transmit1( unsigned char data ){
   while ( !( UCSR0A & (1<<UDRE0)) ) {  }; 
   UDR0 = data;   
   while ( !( UCSR0A & (1<<TXC0)) ) {  }; 

 } 


void USART_sendBuffer(uint16_t length) {

  from_cpc_input_buffer_index = 0; 

  // READY_OFF;   
  // TRANSMIT_ON_ONLY; 
  // TRANSMIT_ON; 
  // usart_tx_on(); 

  for (uint8_t i = 0; i < length; i++) {
    while ( !( UCSR0A & (1<<UDRE0)) ) { }; 
    UDR0 = send_msg[i];   
  }

  //while ( !( UCSR0A & (1<<TXC0)) ) {  }; 
  //usart_tx_off(); 

}

//
//
//


#define serial_main_loop_read(databus) { READY_ON; cli(); serial_ready; sei(); loop_until_write_happened; midi_serial_busy; loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); DATA_FROM_CPC(databus);  READY_OFF; }

#define serial_main_loop_read2(databus) { READY_ON; cli(); midi_serial_ready2; sei(); loop_until_write_happened; midi_serial_busy; loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); DATA_FROM_CPC(databus);  READY_OFF; }

#define serial_main_loop_read_no_ready(databus) { READY_ON; loop_until_write_happened; loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); DATA_FROM_CPC(databus); READY_OFF; }

//
//
//

void usart_mode_loop(void) {

  uint8_t direct_mode = 0;  
  uint8_t dummy = 0;  
  
  serial_busy;  

  LEDS_OFF; 

  command_confirm("Serial mode. Serial commands start with 255."); 

  usart_input_buffer_index = 0; 
  from_cpc_input_buffer_index = 0; 
  cpc_read_cursor = 0; 
  bytes_available = 0; 
 
  z80_run; 
  sei(); 
  
  usart_init(); 

  SERIAL_ON; 
  
  CUR_MODE = SERIAL_M; 

  stop_timer; 

  enable_write_isr(); 

  while (1) {

    serial_main_loop_read(databus); 

    if (databus == 255) {

      // command sent! 
      // receive command byte - what to do? 
 
      serial_main_loop_read2(databus); 

      if (databus == 255) {
	
	// escape: 255 255 -> 255 ! 

	if (direct_mode) {
	  USART_Transmit1(databus); 
	} else {

	  if (from_cpc_input_buffer_index < (SEND_BUFFER_SIZE-1)) {
	    send_msg[ from_cpc_input_buffer_index ] = databus;  
	    // max index is hence SEND_BUFFER_SIZE-1 - save to loop through the whole array
	    from_cpc_input_buffer_index++;
	  }   
	}
	 
      } else {
	
	byte_avail = bytes_available > 0 ? 1 : 0; 
	databus1 = 0; 
	
	if (byte_avail ) {  
	  if ( cpc_read_cursor < SPEECH_BUFFER_SIZE) {
	    databus1 = buffer[ cpc_read_cursor ]; 
	  } 
	}

	// dispatch, decode command byte

	switch (databus) {

	case 10 : // USART MONITOR OLD - CANNOT TAKE COMMANDS FROM CPC - OBSOLETE FOR NOW!

	  usart_input_buffer_index = 0; 
	  SERIAL_ON; 

	  usart_init(); 

	  cpc_read_cursor = 0;  
	  LEDS_OFF; 

	  serial_ready;

	  while (1) {
	    
	    serial_main_loop_read_no_ready(dummy); 

	    databus = (cpc_read_cursor != usart_input_buffer_index); 
	    cli(); DATA_TO_CPC(databus); sei(); 
	    	    
	    if (databus) {
	    
	      if (cpc_read_cursor < SPEECH_BUFFER_SIZE) {
		databus = buffer[cpc_read_cursor]; 
		cpc_read_cursor++;

		if ( cpc_read_cursor == SPEECH_BUFFER_SIZE) 
		  cpc_read_cursor = 0; 
	      }
	      
	      serial_main_loop_read_no_ready(dummy); 
	      cli(); DATA_TO_CPC(databus); sei(); 
	      
	      USART_Transmit1(databus); 
	    	      
	    }	 

	  } // end while - cannot exit here! 

	  break;

	case 50 : // USART MONITOR NEW - WITH COMMANDS FROM CPC 

	  usart_input_buffer_index = 0; 
	  cpc_read_cursor = 0;  

	  SERIAL_ON; 
	  usart_init(); 

	  LEDS_OFF; 

	  databus = 0; 
	  databus1 = 0; 
	  databus2 = 0; 

	  bytes_available = 0; 

	  while (1) {

	    serial_main_loop_read_no_ready(databus); 

	    // first byte: determine mode 

	    if (databus1 == 0 || databus1 == 2) { // 0 or 2: 
	      switch (databus) {

	      case 1 : databus1 = 1; break;  // send byte when 2nd byte received
	      case 2 : 
		databus1 = 0; 
		byte_avail = bytes_available > 0 ? 1 : 0; 
		// this is 0 or 1, midi_ready_signal is 2, so we cannot confuse the 2 
		if (byte_avail) {
		  // save the byte, in case the buffer overflows... 
		  if (cpc_read_cursor < SPEECH_BUFFER_SIZE) {
		    databus2 = buffer[cpc_read_cursor]; 
		  }	      
		}
		
		cli(); DATA_TO_CPC(byte_avail); sei(); 
		break; 

	      case 3 : 
		// signal availability and give CPC time to read: 
		// this is 0 or 1, midi_ready_signal is 2, so we cannot confuse the 2 
		if (databus1 == 0) {
		  // deliver low nibble of current databus2 byte 
		  // make sure value > 15 to distinguish from synchronization / ready signals!!
		  databus = ( databus2 & 0b00001111 ) << 4 | 0b00001111; 
		  databus1 = 2; 
		} else { 
		  // databus1 == 2: 
		  // deliver high nibble of current databus2 byte  
		  databus = ( databus2 & 0b11110000 ) | 0b00001111;  
		  databus1 = 0; 
		  if (byte_avail) {
		    bytes_available--; 
		    cpc_read_cursor++;
		    if ( cpc_read_cursor == SPEECH_BUFFER_SIZE) 
		      cpc_read_cursor = 0; 
		  } 
		} 

		cli(); DATA_TO_CPC(databus); sei(); 
		break; 

	      case 4 : 
		
		disable_write_isr(); 		
		return; 
		break; 
	      } 
	    } else {
	      // databus1 == 1: transmit received byte
	      USART_Transmit1(databus); 
	      databus1 = 0; 	     
	    }	 
	  } // end while - cannot exit here! 
	  
	  break;	  	  

	case 1 :  // write USART single byte 

	  serial_main_loop_read(databus); 
	  USART_Transmit1(databus); 

	  break; 

	case 2 :  // print buffer to serial 

	  USART_sendBuffer(from_cpc_input_buffer_index); 

	  break; 
	  
	case 3 ... 4 : {

	  uint16_t delta = bytes_available; 

	  if (databus == 3) {
	    delta &= 0xFF; 
	  } else {
	    delta >>= 8; 
	  }
	  
	  SEND_TO_CPC_DATABUS( delta ); 

  	  } 	
	   
	  break; 
	  
	case 5 : // ask if buffer is full 

	  SEND_TO_CPC_DATABUS(bytes_available == (SPEECH_BUFFER_SIZE - 1)); 

	  break; 

	case 6 : // flush receive buffer 

	  usart_input_buffer_index = 0; 
	  cpc_read_cursor = 0; 
	  bytes_available = 0; 

	  break; 
	  
	case 7 : // check if byte available 

	  SEND_TO_CPC_DATABUS( byte_avail ); 

	  break; 

	case 8 : // get byte for CPC at current USART input buffer position

	  SEND_TO_CPC_DATABUS( databus1 ); 
	    
	  break; 

	case 9 : // get next byte for CPC in USART input buffer

	  SEND_TO_CPC_DATABUS( databus1 ); 

	  if (byte_avail ) {
	      cpc_read_cursor++;
	      bytes_available--; 
	      if (cpc_read_cursor == SPEECH_BUFFER_SIZE ) {
		// wrap around
		cpc_read_cursor = 0;       
	      }
	  }

	  break;

	case 11 : // set cursor to given byte position 

	  serial_main_loop_read(databus); 
	  serial_main_loop_read(databus1); 
	  cpc_read_cursor = databus + (databus1 << 8); 
	  	  
	  break;

	case 12 : // reset CPC cursor 

	  cpc_read_cursor = 0; 
	  
	  break; 

	case 13 : // set CPC cursor to last byte 

	  if (usart_input_buffer_index > 0) {
	    cpc_read_cursor =  usart_input_buffer_index-1; 
	  }
	  
	  break; 

	case 14 :  // check status 

	  //z80_run; 
	  SEND_TO_CPC_DATABUS(direct_mode); 

	  break; 

	case 15 : // announce mode!
	  
	  LAMBDA_EPSON_ON; 	  	    
	  sprintf(command_string, "Serial interface active at %lu bauds, %d bits, %d stop bits, and %s parity. %s mode.", 
		  SERIAL_RATE, SERIAL_WIDTH, SERIAL_STOP_BITS, 
		  ( SERIAL_PARITY == 1 ? "odd" : ( SERIAL_PARITY == 2 ? "even" : "no" )),
		  (direct_mode == 1 ? "direct" : "buffered" )); 

	  command_confirm(command_string); 
	  
	  usart_input_buffer_index = 0; 
	  from_cpc_input_buffer_index = 0; 
	  cpc_read_cursor = 0; 
	  bytes_available = 0; 

	  SERIAL_ON; 

	  break; 

	case 16 :  // turn on databus direct printing 

	  direct_mode = 1;  

	  usart_input_buffer_index = 0; 
	  from_cpc_input_buffer_index = 0; 
	  cpc_read_cursor = 0; 
	  bytes_available = 0; 

	  break; 

	case 17 :  // turn off databus direct printing  -> print to buffer! 

	  direct_mode = 0;  

	  usart_input_buffer_index = 0; 
	  from_cpc_input_buffer_index = 0; 
	  cpc_read_cursor = 0; 
	  bytes_available = 0; 

	  break; 

	case 18 :  // get current cpc read cursor position 

	  //z80_run; 
	  SEND_TO_CPC_DATABUS(cpc_read_cursor); 

	  break; 

	case 20 : 
	  
	  // z80_run; 

	  usart_off(); 	   
	  LAMBDA_EPSON_ON; 	  	    
	  serial_busy;  

	  // BLOCKING = 1; 
	  // NON_BLOCK_CONFIRMATIONS = 0; 
	  command_confirm("Quitting serial mode."); 

	  DATA_TO_CPC(0);   
	  CUR_MODE = START_OVER_SAME_MODE; 
	  // process_reset(); 

	  disable_write_isr(); 

	  return; 

	  break;  

	case 30 : // set BAUDRATE

	  serial_main_loop_read(SERIAL_BAUDRATE); 
	  usart_init(); 

	  break; 

	case 31 : // set WIDTH

	  serial_main_loop_read(SERIAL_WIDTH); 
	  usart_init(); 

	  break; 

	case 32 : // set PARITY

	  serial_main_loop_read(SERIAL_PARITY); 
	  usart_init(); 
 
	  break; 

	case 33 : // set STOP_BITS

	  serial_main_loop_read(SERIAL_STOP_BITS); 
	  usart_init(); 

	  break; 

	case 55 : // TEST  
	  
	  serial_main_loop_read(databus); 
	  cli(); DATA_TO_CPC(databus); sei(); 
	  
	  break; 

	case 0xF2 :

	  get_full_mode(); 

	  break; 

	case 0xC3 : 
	  LAMBDA_EPSON_ON; 	  	    
	  announce_cur_mode(); 
	  SERIAL_ON; 

	  break; 
 
	}
      } 

    }  else {
	
      // not a control byte, send or buffer: 
	
      if (direct_mode) {

	USART_Transmit1(databus); 

      } else

	if (from_cpc_input_buffer_index < (SEND_BUFFER_SIZE-1)) {
	  send_msg[ from_cpc_input_buffer_index ] = databus;  
	  // max index is hence SEND_BUFFER_SIZE-1 - save to loop through the whole array
	  from_cpc_input_buffer_index++;
	}   	
    }   
  }
}


//
// Control Byte Dispatching 
// 


void process_control(uint8_t control_byte) {

  LEDS_ON; 

  stop();  

  if (NON_BLOCK_CONFIRMATIONS) {
    z80_run;
  }

 
  switch ( control_byte ) {
 
    case 0xFF : process_reset(); break; 

    // DIFFERENT FROM LS3 - AMDRUM MODE SETTINGS
    case 0xFE : use_std_amdrum(); break; 
    case 0xFD : use_custom_amdrum(); break; 
    case 0xFC : use_hq_amdrum(); break;      

    case 0xF4 : non_blocking_confirmations(); break; 
    case 0xF3 : blocking_confirmations(); break;  
    case 0xF2 : get_full_mode(); break; 
    case 0xF1 : usart_mode_loop(); break; 

    case 0xEF : native_mode_epson(); break; 
    case 0xEE : native_mode_dectalk(); break; 
    case 0xED : ssa1_mode(); break; 
    case 0xEC : dktronics_mode(); break; 
    case 0xEB : non_blocking(); break; 
    case 0xEA : blocking(); break; 
    case 0xE9 : confirmations_on(); break;  
    case 0xE8 : confirmations_off(); break;   
    case 0xE7 : english(); break; 
    case 0xE6 : spanish(); break; 
    case 0xE5 : fast_getters(); break; 
    case 0xE4 : slow_getters(); break;
 
    case 0xE3 : if (CUR_AMDRUM_MODE == AMDRUM_CUSTOM) 
                   amdrum_mode_custom(); 
                else if (CUR_AMDRUM_MODE == AMDRUM_HQ) 
                   amdrum_mode_hq(); 
                else amdrum_mode(); break; 

    case 0xE2 : handshake_getters(); break;

    case 0xE0 : medium_getters(); break; 

    case 0xDF : break;   // stop_command(); 
    case 0xDE : flush_command(); break;

    case 0xDD : i2c_speak_time(); break; 
    case 0xDC : i2c_speak_date(); break; 
    case 0xDB : i2c_set_time(); break; 
    case 0xDA : i2c_set_date(); break;  
    case 0xD3 ... 0xD9 : 
                if ( CUR_MODE == LAMBDA_EPSON_M || CUR_MODE == LAMBDA_DECTALK_M ) i2c_get_clock_reg(control_byte - 0xD3); break;       
    case 0xD2 : if ( CUR_MODE == LAMBDA_EPSON_M || CUR_MODE == LAMBDA_DECTALK_M ) i2c_get_temp(); break;  
    case 0xD1 : i2c_speak_temp(); break; 

    case 0xCF : get_mode(); break; 
    case 0xCE : get_volume(); break; 
    case 0xCD : get_voice(); break; 
    case 0xCC : get_rate(); break; 
    case 0xCB : get_language(); break; 
    case 0xCA : get_delay(); break; 
    case 0xC9 : get_version(); break; 
    case 0xC8 : speak_copyright_note(); break; 
    case 0xC7 : speak_hal9000_quote(); break; 
    case 0xC6 : sing_daisy(); break; 
    case 0xC5 : echo_test_program(); break; 
    case 0xC4 : echo_program(); break; 
    case 0xC3 : announce_cur_mode(); break; 
    case 0xC2 : pcm_test(); break; 
    case 0xC1 : echo_byte(); break; 

    case 0xB0 : set_voice_default(); break;
    case 0xB1 ... 0xBD : set_voice( control_byte - 0xB0); break; 

    case 0xA0 : set_volume_default(); break;
    case 0xA1 ... 0xAF : set_volume( control_byte - 0xA0); break;

    case 0x90 : set_rate_default(); break;
    case 0x91 ... 0x9F : set_rate( control_byte - 0x90); break;

    case 0x80 : set_buffer_delay_default(); break;
    case 0x81 ... 0x8F : set_buffer_delay( control_byte - 0x80 ); break;

    }

  LEDS_OFF; 
  
  proceed(); 

  return; 
 
}

//
//
//

int main(void) {

  // uint8_t diag = MCUSR;

  MCUSR = 0;
  MCUSR = 0;

  wdt_init();  // soft reset init 

  // disable JTAG ! SET IT TWICE REALLY!!!
  MCUCR=0x80;// for the atmega644
  MCUCR=0x80;// for the atmega644

  //
  //
  //

  init_pins();  
  LEDS_OFF;

  //
  // diagnostics 
  //

  /* 
     if (bit_is_set(diag, JTRF)) {
     blink_led_slow(1);
     }

     if (bit_is_set(diag, WDRF)) {
     blink_led_slow(2);
     }
  
     if (bit_is_set(diag, BORF)) {
     blink_led_slow(3);
     }

     if (bit_is_set(diag, EXTRF)) {
     blink_led_slow(4);
     }

     if (bit_is_set(diag, EXTRF)) {
     blink_led_slow(5);
     }

     blink_led_fast(100); 
    
  */ 

  //
  //
  //

  init_reset_handler();   
  init_allophones();  

  blink_led_fast(20); 

  tts_setup(); 
  
  //
  //
  //

  SP0256_OFF;

  //
  //
  //

#ifdef BOOTMESSAGE  
  speech_ready_message(); 
#endif 
  tts_configure_dectalk();

  //
  //
  //

  /*
 
    uint8_t a = 250;
    uint8_t b = 5; 

    uint8_t diff = b - a;

    sprintf(command_string, "Result is %d ", diff);

    command_confirm(command_string); 

  */

  sei(); 

  // 
  // Test
  // 

  /* 
  usart_init(); 

  while (1) {

    DATA_TO_CPC( WRITE_HAPPENED );     

  }
  */

  //
  //
  //

  while (1) {

    //
    // main loop 
    //

    if (CUR_MODE == DKTRONICS_M) {      

      SP0256_OFF; 
      EPSON_DK_ON; 

      configure_speak_buffer_timer; 
      start_timer; 

      dk_speech_busy;      
      z80_run; 

      // DK'Tronics Emulation
 
      TRANSMIT_OFF;
      READY_ON; 
      
      while (1) {

	dk_speech_idle_loadme;

	loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE_DK); 
	dk_speech_busy; 
	stop_timer;

	_delay_us(3); // don't change... it works

	DATA_FROM_CPC(databus); 	  

	if (databus & 0b10000000 ) {

	  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE_DK); 

	  if (databus == 0xF0) { 
	    // do nothing for next byte! just for databus LED display 

	    wait_and_echo_single_byte(); 	    

	  } else {

	    z80_halt; 	  
	    process_control(databus); 
	    z80_run; 
	    
	    if (CUR_MODE != LAST_MODE) { 
	      
	      if (CUR_MODE == START_OVER_SAME_MODE)
		CUR_MODE = LAST_MODE; 
	      else
		LAST_MODE = CUR_MODE; 
	      
	      break; 

	    } 
	  }

	} else {

	  _delay_us(15);
	  start_timer; 

	  buffer[length++] = databus;
	  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE_DK); 

	  if (length == SPEECH_BUFFER_FLUSH_AT)  {
	    speak_buffer(); 
	    if ( STOP_NOW ) {
	      blink_leds(); 
	      STOP_NOW = 0; 
	      tts_stop(); 
	      proceed(); 
	    }
	  }
	}
      }

    } else if (CUR_MODE == SSA1_M) {      

      //
      // SSA1 
      //

      SP0256_OFF; 
      EPSON_SSA1_ON; 

      configure_speak_buffer_timer; 
      start_timer; 

      speech_busy;      
      z80_run; 

      // SSA1 Emulation
 
      TRANSMIT_OFF;
      READY_ON; 
      
      while (1) {

	if (emulated_ssa1_buffer_size == 0) {
	  speech_idle_loadme;
	} else {
	  speech_speaking_loadme;
	}

	loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 
	speech_busy; 
	stop_timer;

	_delay_us(3); // don't change... it works

	DATA_FROM_CPC(databus); 	  

	if (databus & 0b10000000 ) {

	  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

	  if (databus == 0xF0) { 
	    // do nothing for next byte! just for databus LED display 

	    wait_and_echo_single_byte(); 	    

	  } else {
	    
	    z80_halt; 	  
	    process_control(databus); 
	    z80_run; 
	    
	    if (CUR_MODE != LAST_MODE) {
	      
	      if (CUR_MODE == START_OVER_SAME_MODE)
		CUR_MODE = LAST_MODE; 
	      else
		LAST_MODE = CUR_MODE; 
	      
	      break; 

	    }
	  }

	} else {

	  _delay_us(15);
	  start_timer; 
	  
	  buffer[length++] = databus;
	  emulated_ssa1_buffer_size = 1; 

	  loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

	  if (length == SPEECH_BUFFER_FLUSH_AT)  {
	    speak_buffer(); 
	    if ( STOP_NOW ) {
	      blink_leds(); 
	      STOP_NOW = 0; 
	      tts_stop(); 
	      proceed(); 
	    }
	  }
	}
      }

    } else {	 

      //
      // LambdaSpeak native mode - dectalk or epson parser
      // 

      SP0256_OFF; 
      if (CUR_MODE == LAMBDA_EPSON_M ) {
        LAMBDA_EPSON_ON; 
      } else { 
        LAMBDA_DECTALK_ON; 
      }

      stop_timer; 

      uint8_t ctrl = 0; // \xAB, \ = 1, x = 2, A = 3, B = 4 
      uint8_t nibble1 = 0; 
      uint8_t nibble2 = 0; 
      uint8_t log_in = 0; 

      while (1) {

	TRANSMIT_OFF;
	READY_ON;

	speech_native_ready; 
	z80_run; 	
	
	loop_until_bit_is_set(IOREQ_PIN, IOREQ_WRITE); 

	DATA_FROM_CPC(databus); 

	loop_until_bit_is_clear(IOREQ_PIN, IOREQ_WRITE); 

	speech_native_busy;  // to 0... 

	TRANSMIT_ON;
	READY_OFF;

	if (databus & 0b10000000 ) {
	  
	  if (databus == 0xF0) { 
	    // do nothing for next byte! just for databus LED display 

	    wait_and_echo_single_byte(); 

	  } else {

	    z80_halt;

	    process_control(databus); 

	    if (!BLOCKING)
	      z80_run; 

	    if (CUR_MODE != LAST_MODE) {

	      if (CUR_MODE == START_OVER_SAME_MODE)
		CUR_MODE = LAST_MODE; 
	      else
		LAST_MODE = CUR_MODE; 
	      
	      break; 
	    }
	    
	  }
	} else {
	  
	  log_in = 0; 
	    
	  if ( databus == '\\' && ctrl == 0 ) {
	    ctrl = 1; 
	  } else if ( ctrl == 1 ) { 
	    if ( databus == 'x' ) {
	      ctrl = 2; 
	    } else {
	      // didn't get \x escape, so log in \ and current: 
	      buffer[length++] = '\\'; 
	      
	      log_in = 1; 
	      ctrl = 0; 
	    }
	  } else if ( ctrl == 2 ) { 
	    if ( databus >= '0' && databus <= '9' ) {
	      nibble1 = databus - '0'; 
	      ctrl = 3; 
	    } else if ( databus >= 'A' && databus <= 'F' ) {
	      nibble1 = databus - 'A'; 
	      ctrl = 3; 
	    } else {
	      // failure to parse \xHEX HEX, ignore! 
	      ctrl = 0; 
	    }
	  } else if ( ctrl == 3 ) {
	    if  ( databus >= '0' && databus <= '9' ) {
	      nibble2 = databus - '0'; 
	      databus = 16*nibble1 + nibble2; 
	      ctrl = 0; 
	      log_in = 1; 
	    } else if ( databus >= 'A' && databus <= 'F' ) {
	      nibble2 = databus - 'A'; 
	      databus = 16*nibble1 + nibble2; 
	      ctrl = 0; 
	      log_in = 1; 
	    } else { 
	      ctrl = 0;  
	    }
	  } else {
	    // normal character
	    ctrl = 0; 
	    log_in = 1; 	    
	  }
	  
	  if (log_in) {

	    buffer[length++] = databus;

	    if (length >= SPEECH_BUFFER_FLUSH_AT)  {
	    
	      if (!BLOCKING)
		z80_run; 

	      speak_buffer(); 

	      if ( STOP_NOW ) {
		blink_leds(); 	      
		STOP_NOW = 0; 
		tts_stop();
		proceed();  
	      }

	    } else if ( databus == 13) {

	      if (!BLOCKING)
		z80_run; 
	    
	      speak_buffer(); 	  

	      if ( STOP_NOW ) {
		blink_leds(); 	      
		STOP_NOW = 0; 
		tts_stop(); 
		proceed(); 
	      }
	    }
	  }
	}
      }
    }

    //
    // main loop  
    // 

  }
 
  return 0;  
   
}

 
