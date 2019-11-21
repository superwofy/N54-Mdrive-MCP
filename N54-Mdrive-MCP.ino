#include <mcp_can.h>

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];
byte key_normal[] = {0xFF, 0x3F, 0x00};
byte key_pressed[] = {0xBF, 0x7F, 0xF9};
byte sndstat = CAN_OK;

MCP_CAN CAN0(10);                               // Set CS to pin 10

void setup(){
  Serial.begin(115200);
  
  // Initialize MCP2515
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
      CAN0.sendMsgBuf(0x1D9, 0, 3, key_normal);  //no key pressed. Send every second to keep ECU happy. Timed with the MFL status.
    } else {
      sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, key_pressed);
      while (sndstat != CAN_OK) {
        Serial.println("Failed to send key pressed message. Re-trying.");
        sndstat = CAN0.sendMsgBuf(0x01D9, 0, 3, key_pressed);
      }
      Serial.println("Sent key press.");
    }
  }
}
