#include <mcp_can.h>

long unsigned int rxId;
unsigned char len = 0, rxBuf[8];
int key_pressed_counter = 0, i = 0x00;
byte sndstat = CAN_OK, dtc_pressed[] = {0xFD, 0xFF}, dtc_normal[] = {0xFC, 0xFF};

MCP_CAN CAN0(10);                               // Set CS to pin 10

void setup(){
  Serial.begin(115200);
  if(CAN0.begin(MCP_STDEXT, CAN_500KBPS, MCP_16MHZ) == CAN_OK) Serial.println("MCP2515 Initialized Successfully!");
  else Serial.println("Error Initializing MCP2515...");

  pinMode(2, INPUT);                             // Configuring pin for /INT input, pin 2
  CAN0.init_Mask(0,0,0x07FFFFFF);                // Init masks and filters
  CAN0.init_Mask(1,0,0x07FFFFFF);                // Mask matches: 07FF (standard ID), first byte FF, second byte FF
  CAN0.init_Filt(0,0,0x01D6C00C);                // Filter, no MFL key pressed.
  CAN0.init_Filt(1,0,0x01D6C04C);                // Filter, source key pressed
  
  CAN0.setMode(MCP_NORMAL);                     
}

void loop(){
  if (!digitalRead(2)) {                         // If pin 2 is low, read receive buffer
    CAN0.readMsgBuf(&rxId, &len, rxBuf);         // Read data: len = data length, buf = data byte(s)
    if (rxBuf[1] == 0x0C) {
      byte key_normal[] = {0xFF, 0x3F, i};
      CAN0.sendMsgBuf(0x1D9, 0, 3, key_normal);  //no key pressed. Send every second to keep ECU happy. Timed with the MFL status.
      key_pressed_counter = 0;
    } else {
      byte key_pressed[] = {0xBF, 0x7F, i};
      sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, key_pressed);
      while (sndstat != CAN_OK) {
        Serial.println("Failed to send key pressed message. Re-trying.");
        sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, key_pressed);
      }
      key_pressed_counter++;
      if (key_pressed_counter == 10) send_dtc();
      Serial.println("Sent key press.");
    }
    i++;                                        //i is used as the message counter
    if (i == 0x100) i = 0x00;
  }
}

void send_dtc(){                                  //This exact sequence is required. If the timing is off, DSC-ecu will ignore the message.
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_pressed);      //Adding enough (about 30) dtc_pressed will turn DSC off. Two are sent during a quick press of the button.
  delay(100);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_pressed);
  delay(50);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);       //Three dtc_normal are always sent at the end 160ms apart as a sort of checksum.
  delay(160);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);
  delay(160);
  CAN0.sendMsgBuf(0x316, 0, 2, dtc_normal);
  key_pressed_counter = 0;
}
