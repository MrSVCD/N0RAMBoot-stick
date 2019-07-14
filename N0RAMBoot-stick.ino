/*
  Copyright 2019 Pär Moberg

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  --------------------------------------------------------------------------------

  Usage:
  Put the data you want to upload to your Sega Master System in data.h
  The data needs to be a multiple of 128bytes
  (I hope to fix this in a future release and skipping the truncate step).

  If you run or have a unix environment/tools on hand you can use truncate and xxd
  to generate the data in data.h
  ------------
  First use truncate to extend (unintuitive I know) your binary to nearest larger
  128byte block.

  This will change your original file and add 0x00s to the end of your file.

  To nearest larger block:
  truncate -s %128 (your binary file)
  ------------

  After that you can convert your file to a include file with xxd:
  xxd -i (your file) data.h
  ------------

  last step is to change the first line in data.h to:
  const PROGMEM byte data[] = {

  I believe that you don't need to remove the last line in your generate data.h
  and that the compiler will ignore it.
  ------------

  The demo data in data.h is a inverted png image of the connection diagram from
  N0RAMBoot, which you can receive on your computer with xmodem over 9600 8N1.
  (I hope to change this to a SMS hello world in the future.)

  Another future feature is to have multiple games/programs on one stick and
  pass thru of player two gamepad but that would need more complex hardware to
  disable the gamepad during transfer (or just a strict message to not touch the
  controller).
  ------------

  WARNING, This is untested!

  Hardware:
  Arduino Uno       Master System
  Or similar        Player 2 port

  Digital 0 (RX) <-[330R]- Pin 9

  Digital 1 (TX) -[330R]-> Pin 7

  GND -------------------- Pin 8

  +5V -------------------- Pin 5

  I have not tested this connection diagram and if it works with or without the
  330 ohm resistors.
*/

#include <avr/pgmspace.h>
#include "data.h"     //PNG Image from N0RAMBoot
//#include "random.h"   //Random data for speed memory usage test

int numberofblocks = 0;
int datablock = 0;
int incomingByte = 0;
int useCRC = 0;
int sum = 0;
unsigned int CRC = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  numberofblocks = sizeof(data) / 128;
}

void loop() {
}

void serialEvent() {
  incomingByte = Serial.read();                    //Reads a byte.
  if (incomingByte == 82) {                        //Looking for R and reset the datablock counter.
    datablock = 0;

  } else if (incomingByte == 21) {                 //Looking for the NAK control character to start the transfer or resend the last datablock.
    if (datablock == numberofblocks) {
      Serial.write(4);
    } else {
      if (useCRC == 0) {
        sendBlock(datablock);
      } else {
        sendBlockCRC(datablock);
      }
    }
  } else if (incomingByte == 67) {                 //If a "C" is recived, turn on CRC mode and send the first block.
    useCRC = 1;
    datablock = 0;
    sendBlockCRC(datablock);

  } else if (incomingByte == 6) {                  //Looking for the ACK control character to transfer the next datablock or indicate the end of file/data.
    datablock++;                                   //Increment datablock number.

    if (datablock == numberofblocks) {
      Serial.write(4);                             //Sends EOF to let xmodem know that there is nothing left to recive.

    } else if (datablock > numberofblocks) {
      datablock = 0;                               //Reset the datablock counter. Makes it possible to not need to send reset "R".
      useCRC = 0;

    } else {                                       //Send datablock.
      if (useCRC == 0) {
        sendBlock(datablock);
      } else {
        sendBlockCRC(datablock);
      }

    }
  }
}

void sendBlock(int xmodemblock) {
  digitalWrite(LED_BUILTIN, LOW);        //blinks the led with every other block
  sum = 0;                                     //Resets the checksum

  Serial.write(1);                                 //Start Of Heading, the start of block byte
  Serial.write(xmodemblock + 1);                   //Block number, starting with 1
  Serial.write(255 - (xmodemblock + 1));           //Inverted block number
  Serial.flush();                                  //Flushing the serial write cache.

  for (int i = (xmodemblock * 128); i < (xmodemblock + 1) * 128; i++) { //Loop of data transmission and checksum calculation
    Serial.write(pgm_read_byte(&data[i]));                              //pgm_read_byte needs the pointer to read a byte from flash correctly. just data[i] reads from ram.
    sum = 0xff & (sum + pgm_read_byte(&data[i]));                       //checksum calculation and discarding of high byte of the signed int.
    Serial.flush();                                                     //Flushing the serial write cache. better safe than sorry.
  }

  Serial.write(lowByte(sum));                      //Write the checksum of the block, using only the low byte of the signed int.
  Serial.flush();                                  //Probably the only flush needed. Makes the execution stop until write cache is empty.
}

void sendBlockCRC(int xmodemblock) {
  digitalWrite(LED_BUILTIN, HIGH);        //blinks the led with every other block
  CRC = 0;                                     //Resets the checksum

  Serial.write(1);                                 //Start Of Heading, the start of block byte
  Serial.write(xmodemblock + 1);                   //Block number, starting with 1
  Serial.write(255 - (xmodemblock + 1));           //Inverted block number
  Serial.flush();                                  //Flushing the serial write cache.

  for (int i = (xmodemblock * 128); i < (xmodemblock + 1) * 128; i++) { //Loop of data transmission and CRC calculation
    Serial.write(pgm_read_byte(&data[i]));                              //pgm_read_byte needs the pointer to read a byte from flash correctly. just data[i] reads from ram.

    //CRC Calculation
    for (int bitselect = 0; bitselect >= 8; bitselect++) {
      if (CRC & 0x8000) {
        CRC <<= 1;
        bitWrite(CRC,0,bitRead(pgm_read_byte(&data[i]),bitselect));
        CRC ^= 0x1021;
      }
      else {
        CRC <<= 1;
        bitWrite(CRC,0,bitRead(pgm_read_byte(&data[i]),bitselect));
      }
    }
    Serial.flush();                                                     //Flushing the serial write cache. better safe than sorry.
  }

  Serial.write(highByte(CRC));                      //Write the CRC
  Serial.write(lowByte(CRC));
  Serial.flush();                                  //Probably the only flush needed. Makes the execution stop until write cache is empty.
}
