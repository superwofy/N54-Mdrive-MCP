//This sketch toggles sport mode in the IKM0S MSD81 flash in the absence of a DSCM90 or DSCM80 ZB.
//Extra features: holding the M button toggles DTC on/off. Memory.

#include <mcp_can.h>

long unsigned int rxId;
unsigned char len = 0, rxBuf[8];
int mkey_pressed_counter = 0, i = 0x00;
byte sndstat = CAN_OK, mkey_normal[] = {0xFF, 0x3F, 0x00}, mkey_pressed[] = {0xBF, 0x7F, 0x00}, dtc_pressed[] = {0xFD, 0xFF}, dtc_normal[] = {0xFC, 0xFF};
bool ignition_on = false;

MCP_CAN CAN0(10);                                // CS. Adapt to your board
#define CAN0_INT 2                               // INT. Adapt to your board

void setup() {
  Serial.begin(115200);
  if(CAN0.begin(MCP_STDEXT, CAN_500KBPS, MCP_16MHZ) == CAN_OK) Serial.println("MCP2515 Initialized Successfully!");
  else Serial.println("Error Initializing MCP2515.");

  pinMode(CAN0_INT, INPUT);                             // Configuring pin for /INT input, pin 2

  CAN0.init_Mask(0,0,0x07FF0000);                // Mask matches: 07FF (standard ID) and only first byte FF
  CAN0.init_Filt(0,0,0x01300000);                // Filter, ignition

  CAN0.init_Mask(1,0,0x07FFFFFF);                // Mask matches: 07FF (standard ID), first byte FF, second byte FF
  CAN0.init_Filt(2,0,0x01D6C00C);                // Filter, no MFL key pressed.
  CAN0.init_Filt(3,0,0x01D6C04C);                // Filter, source key pressed

  
  CAN0.setMode(MCP_NORMAL);
}

void loop() {
  if (!digitalRead(CAN0_INT)) {                         // If pin 2 is low, read receive buffer
    CAN0.readMsgBuf(&rxId, &len, rxBuf);                // Read data: len = data length, buf = data byte(s)

    if (rxId == 0x130) {
      if (rxBuf[0] == 0x45) ignition_on = true;
      else ignition_on = false;
    }

    else if (rxId == 0x1D6) {                           // MFL actions
      if (ignition_on) {
        if (rxBuf[1] == 0x0C) {
          send_mkeyidle();
          mkey_pressed_counter = 0;
        } else {
          send_mkeypress();
          mkey_pressed_counter++;
          if (mkey_pressed_counter == 6) {
            send_dtc();
          }
        }
      }
    }
  }
}

void send_mkeyidle() {
  mkey_normal[2] = i;
  sndstat = CAN0.sendMsgBuf(0x1D9, 0, 3, mkey_normal);  //no key pressed. Send every second to keep ECU happy. Timed with the MFL status.
  while (sndstat != CAN_OK) {
    Serial.println("Failed to send key idle message. Re-trying.");
    sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, mkey_normal);
  }
  i++;
  if (i == 0x100) i = 0x00;                             // i is used as the message counter
}

void send_mkeypress() {
  mkey_pressed[2] = i;
  sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, mkey_pressed);
  while (sndstat != CAN_OK) {
    Serial.println("Failed to send key pressed message. Re-trying.");
    sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, mkey_pressed);
  }
  i++;
  if (i == 0x100) i = 0x00;
  Serial.println("Sent mkey press.");
}

void send_dtc() {                                 // This exact sequence is required. If the timing is off, DSC module will ignore the message.
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_pressed);      // Adding enough (about 30) dtc_pressed will turn DSC off. Two are sent during a quick press of the button (DTC).
  delay(100);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_pressed);
  delay(50);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);       // Three dtc_normal are always sent at the end 160ms apart as a sort of checksum.
  delay(160);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);
  delay(160);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);
  mkey_pressed_counter = 0;
  Serial.println("Sent dtc key press.");
}
