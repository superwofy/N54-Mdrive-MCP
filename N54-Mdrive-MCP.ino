// This sketch toggles sport mode in the IKM0S MSD81 DME in the absence of a DSCM90 or DSCM80 ZB.
// Extra features: holding the M button (source) toggles DTC on/off, RPM upshift warning on KOMBI (M or non-M)/CCC/CIC CheckControl.
// Built with Longan Serial Can module 1.2 (ATMEGA168PA). Board installed in the LDM housing feeding directly off onboard 5V regulator.
// 16MHz XTAL for 168PA added. MiniCore 16 MHz bootloader installed https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json
// Credit to Trevor for providing 0AA formulas http://www.loopybunny.co.uk/CarPC/can/0AA.html
// CAN communication library https://github.com/coryjfowler/MCP_CAN_lib
 
#include "src/mcp_can.h"


/********************************************************************************************************************************************************************************
  Adjustment section. Configure board here.
********************************************************************************************************************************************************************************/

MCP_CAN CAN0(9);                                                                  // CS pin.  Adapt to your board.
#define CAN0_INT 2                                                                // INT pin. Adapt to your board.
const int MCP2515_CLOCK = 1;                                                      // Set 1 for 16MHZ or 2 for 8MHZ (MCP2515).
#define POWERSAVE 1                                                               // Enable/Disable power saving methods.
#if POWERSAVE
  #include <avr/power.h>
#endif


/********************************************************************************************************************************************************************************
  Adjustment section 2. Configure program functionality here.
********************************************************************************************************************************************************************************/

#pragma GCC optimize ("-Ofast")                                                   // 'Fast' compiler optimisation level.  
#define DEBUG_MODE 0                                                              // Toggle serial debug messages.
#if DEBUG_MODE
  #define EXTENDED_DEBUG_MODE 0                                                   // Even more serial debug messages.
#endif
const int DTC_SWITCH_TIME = 7;                                                    // Set duration for Enabling/Disabling DTC mode on with long press of M key. 100ms increments.
#define UPSHIFT_WARNING 0                                                         // Enable/Disable upshift CC notification.


/****************To change warning type enable one of these only !****************/
#if UPSHIFT_WARNING
  #define UPSHIFT_RPM_CC 26800                                                    // RPM setpoint to display CC notification (warning = desired RPM * 4).
  #define UPSHIFT_WARNING_WITH_LIMIT_CC_RED 0                                     // Replace upshift CC notification with red LIMIT notification.
  #define UPSHIFT_WARNING_WITH_TRIANGLE_CC_YEL 0                                  // Replace upshift CC notification with yellow triangle notification.
  #define UPSHIFT_WARNING_WITH_ACC_INDICATOR_RED 0                                // Replace upshift CC notification with flashing red ACC car LED.
  #define UPSHIFT_WARNING_WITH_ACC_INDICATOR_YEL 0                                // Replace upshift CC notification with flashing yellow ACC car LED.
#endif                                                                            // M3 clusters need to have the LEDs and resistors soldered for ACC!


/********************************************************************************************************************************************************************************
********************************************************************************************************************************************************************************/


long unsigned int rxId;
unsigned char rxBuf[8], len;
int mkey_checksum = 0xF0, mkey_hold_counter = 0;
byte mkey_idle[] = {0xFF, 0x3F, 0}, mkey_pressed[] = {0xBF, 0x7F, 0}, dtc_key_pressed[] = {0xFD, 0xFF}, dtc_key_released[] = {0xFC, 0xFF};
#if UPSHIFT_WARNING
  byte upshift_warning_on[] = {0x40, 0x14, 0x01, 0x31, 0xFF, 0xFF, 0xFF, 0xFF}, upshift_warning_off[] = {0x40, 0x14, 0x01, 0x30, 0xFF, 0xFF, 0xFF, 0xFF};
  bool warning_displayed = false;
  int32_t RPM, throttle_pedal;
  #if UPSHIFT_WARNING_WITH_LIMIT_CC_RED
    upshift_warning_on[1] = upshift_warning_off[1] = 0x3E;
    upshift_warning_on[2] = upshift_warning_off[2] = 0;
  #elif UPSHIFT_WARNING_WITH_TRIANGLE_CC_YEL
    upshift_warning_on[1] = 0x25 = upshift_warning_off[1] = 0x25;
    upshift_warning_on[2] = upshift_warning_off[2] = 0;
  #elif UPSHIFT_WARNING_WITH_ACC_INDICATOR_RED
    upshift_warning_on[1] = upshift_warning_off[1] = 0x1B;
  #elif UPSHIFT_WARNING_WITH_ACC_INDICATOR_YEL
    upshift_warning_on[1] = upshift_warning_off[1] = 0x1A;
  #endif
#endif

#if DEBUG_MODE
  byte send_stat;
  char dbg_string[128];
#endif

void setup() 
{
  #if POWERSAVE
    disable_unused_peripherals();
  #endif
  #if DEBUG_MODE
    Serial.begin(115200);
    while(!Serial);
  #endif
  while (CAN_OK != CAN0.begin(MCP_STDEXT, CAN_500KBPS, MCP2515_CLOCK)) {
    #if DEBUG_MODE
      Serial.println("Error Initializing MCP2515. Re-trying...");
    #endif
    delay(3000);
  }
  #if DEBUG_MODE
    Serial.println("MCP2515 Initialized Successfully!");
  #endif

  pinMode(CAN0_INT, INPUT);                                                       // Configuring pin for (INT)erupt pin 2
  CAN0.init_Mask(0, 0, 0x07FF0000);                                               // Mask matches: 07FF (standard ID) and all bytes
  CAN0.init_Mask(1, 0, 0x07FF0000);                                               // Mask matches: 07FF (standard ID) and all bytes
  CAN0.init_Filt(0, 0, 0x01D60000);                                               // Filter MFL status. 
  #if UPSHIFT_WARNING
    CAN0.init_Filt(1, 0, 0x00AA0000);                                             // Filter RPM, throttle pos.
  #endif
  CAN0.setMode(MCP_NORMAL);                                                       // Normal mode
}



void loop() 
{                                                                                 // Running as long as the PT-CAN (30G if installed in LDM) is active.
  if (!digitalRead(CAN0_INT)) {                                                   // If INT pin is pulled low, read receive buffer
    CAN0.readMsgBuf(&rxId, &len, rxBuf);                                          // Read data: rxId = CAN ID, buf = data byte(s)
   
    if (rxId == 0x1D6) {       
      if (rxBuf[1] == 0x4C) {                                                     // M button is pressed
        send_mkey_message(mkey_pressed);
        mkey_hold_counter++;
        if (mkey_hold_counter == DTC_SWITCH_TIME) {
          send_dtc_pressed();
          mkey_hold_counter = 0;
        }
      } else {                                                                    // If MFL is released or other buttons are pressed then send alive ping.
          send_mkey_message(mkey_idle);
          mkey_hold_counter = 0;
      }
    } 

    #if UPSHIFT_WARNING
      else if (rxId == 0xAA) {
        RPM = ((int32_t)rxBuf[5] << 8) | (int32_t)rxBuf[4];
        if (RPM >= UPSHIFT_RPM_CC) {
          throttle_pedal = ((int32_t)rxBuf[3] << 8) | (int32_t)rxBuf[2];
          #if EXTENDED_DEBUG_MODE
            sprintf(dbg_string, "Current RPM: %d Pedal position: %d\n", RPM / 4, throttle_pedal);
            Serial.print(dbg_string);
          #endif
          if (throttle_pedal > 3) {                                               // Send the warning only if the throttle pedal is pressed 
            send_upshift_warning();
          }
        }    
      }
    #endif 
  }
}

#if UPSHIFT_WARNING
void send_upshift_warning()
{
  #if UPSHIFT_WARNING_WITH_ACC_INDICATOR_RED || UPSHIFT_WARNING_WITH_ACC_INDICATOR_YEL
    if (warning_displayed)
      CAN0.sendMsgBuf(0x59C, 8, upshift_warning_off);
    else
      CAN0.sendMsgBuf(0x59C, 8, upshift_warning_on);
    warning_displayed = !warning_displayed;
  #else
    CAN0.sendMsgBuf(0x59C, 8, upshift_warning_on);
    delay(50);                                                                
    CAN0.sendMsgBuf(0x59C, 8, upshift_warning_off);
    delay(50);
  #endif   
                                                  
  #if DEBUG_MODE
    Serial.println("Sent upshift warning.");
  #endif
}
#endif

void send_mkey_message(byte message[]) 
{
  message[2] = mkey_checksum;
  byte send_stat = CAN0.sendMsgBuf(0x1D9, 3, message);

  #if DEBUG_MODE
  if (send_stat != CAN_OK)
    Serial.println("Error sending mkey message.");
  #endif
  
  #if EXTENDED_DEBUG_MODE
    sprintf(dbg_string, "cs: %x ", mkey_checksum);
    Serial.print(dbg_string);
  #endif

  if (mkey_checksum < 0xFF)
    mkey_checksum++;
  else
    mkey_checksum = 0xF0;                                                         // mkey_checksum is between F0..FF

  #if DEBUG_MODE
    if (message[0] == 0xFF) 
      Serial.println("Sent mkey idle.");
    if (message[0] == 0xBF) 
      Serial.println("Sent mkey press.");
  #endif
}

void send_dtc_pressed() 
// Correct timing sequence as per trace is: 
//  key press -> delay(100) -> key press -> delay(50) -> key release -> delay(160) -> key release -> delay(160)
// However, that interferes with program timing. A small delay will still be accepted.
{                                                                        
    CAN0.sendMsgBuf(0x316, 2, dtc_key_pressed);                                   // Two messages are sent during a quick press of the button (DTC mode).
    delay(5);
    CAN0.sendMsgBuf(0x316, 2, dtc_key_pressed);
    delay(5);
    CAN0.sendMsgBuf(0x316, 2, dtc_key_released);                                  // Send one DTC released to indicate end of DTC key press.
    #if DEBUG_MODE                        
      Serial.println("Sent DTC key press.");
    #endif
}

#if POWERSAVE
void disable_unused_peripherals()
// Lower power consumption slightly by disabling unused interfaces.
// Do not disable uart on 168P! Configure this function based on MCU.
{
  power_twi_disable();                                                            // Disable I2C.
  power_timer1_disable();                                                         // Disable unused timers. 0 still needed.
  power_timer2_disable();
  ADCSRA = 0;
  power_adc_disable();                                                            // Disable Analog to Digital converter.
}
#endif
