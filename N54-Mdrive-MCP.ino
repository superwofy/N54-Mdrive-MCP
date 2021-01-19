//This sketch toggles sport mode in the IKM0S MSD81 flash in the absence of a DSCM90 or DSCM80 ZB.
//Extra features: holding the M button toggles DTC on/off.
//Configured for Longan Serial Can 1.2
//Toggle debug serial messages from line 39 of mcp_can_dfs.h

                                                    
#define CAN0_INT 2                                  // INT. Adapt to your board
#define DTC_CONTROL 1                               // Enable/Disable DTC toggling with M key

#include "src/mcp_can.h"
MCP_CAN CAN0(9);                                   // CS. Adapt to your board

long unsigned int rxId;
unsigned char rxBuf[8];
int checksum = 0xF0;
byte sndstat, mkey_idle[] = {0xFF, 0x3F, 0xF0}, mkey_pressed[] = {0xBF, 0x7F, 0xF0};
#if DTC_CONTROL
byte dtc_pressed[] = {0xFD, 0xFF}, dtc_idle[] = {0xFC, 0xFF};
int mkey_pressed_counter = 0;
#endif

void setup() {
  
#if DEBUG_MODE
  Serial.begin(115200);
  if(CAN0.begin() == CAN_OK) Serial.println("MCP2515 Initialized Successfully!");
  else Serial.println("Error Initializing MCP2515.");
#else
  CAN0.begin();
#endif

  pinMode(CAN0_INT, INPUT);                     // Configuring pin for /INT input, pin 2

  CAN0.init_Mask(0, 0x07FF0000);                // Mask matches: 07FF (standard ID) and all bytes
  CAN0.init_Mask(1, 0x07FF0000);                // Mask matches: 07FF (standard ID) and all bytes
  CAN0.init_Filt(0, 0x01D6C000);                // Filter MFL status.
  
  CAN0.setMode(MCP_NORMAL);
}

void loop() {   //Simply broadcast 1D9 as long as the PT-CAN is active.
  if (!digitalRead(CAN0_INT)) {                   // If pin 2 is low, read receive buffer
    CAN0.readMsgBuf(&rxId, rxBuf);                // Read data: rxId = CAN ID, buf = data byte(s)
    if (rxBuf[0] == 0xC0 && rxBuf[1] == 0x4C) {
      send_mkey_pressed();
    #if DTC_CONTROL
      if (mkey_pressed_counter == 6) {            // Should be about 6 * 200ms = 1.2 seconds
        send_dtc_pressed();
      }
    #endif
    } else send_mkey_idle();                      // If not C0 4C then send a ping.
  }
}

void send_mkey_idle() {
  mkey_idle[2] = checksum;
  sndstat = CAN0.sendMsgBuf(0x1D9, 3, mkey_idle);
  
  while (sndstat != CAN_OK) {
#if DEBUG_MODE
    Serial.println(sndstat);
    Serial.println("Failed to send key idle message. Re-trying.");
#endif
    sndstat = CAN0.sendMsgBuf(0x01D9, 3, mkey_idle);
  }
  checksum++;
  if (checksum == 0x100) checksum = 0xF0;                             // Checksum is between F0..FF
#if DTC_CONTROL
  mkey_pressed_counter = 0;
#endif
}

void send_mkey_pressed() {
  mkey_pressed[2] = checksum;
  sndstat = CAN0.sendMsgBuf(0x01D9, 3, mkey_pressed);
  while (sndstat != CAN_OK) {
  #if DEBUG_MODE
    Serial.println("Failed to send key pressed message. Re-trying.");
  #endif
    sndstat = CAN0.sendMsgBuf(0x01D9, 3, mkey_pressed);
  }
  checksum++;
  if (checksum == 0x100) checksum = 0xF0;
#if DTC_CONTROL
  mkey_pressed_counter++;
#endif
#if DEBUG_MODE
  Serial.println("Sent mkey press.");
#endif
}

#if DTC_CONTROL
void send_dtc_pressed() {                         // This exact sequence is required. If the timing is off, DSC module will ignore the message.
  CAN0.sendMsgBuf(0x316, 2, dtc_pressed);      // Adding enough (about 30) dtc_pressed will turn DSC off. Two are sent during a quick press of the button (DTC).
  delay(100);
  CAN0.sendMsgBuf(0x316, 2, dtc_pressed);
  delay(50);
  CAN0.sendMsgBuf(0x316, 2, dtc_idle);         // Three dtc_idle are always sent at the end 160ms apart as a sort of checksum.
  delay(160);
  CAN0.sendMsgBuf(0x316, 2, dtc_idle);
  delay(160);
  CAN0.sendMsgBuf(0x316, 2, dtc_idle);
  mkey_pressed_counter = 0;
  #if DEBUG_MODE
  Serial.println("Sent dtc key press.");
  #endif
}
#endif
