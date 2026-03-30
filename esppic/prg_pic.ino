#include "pic_defs.h"

// Programming mode: 0=LVP (Low-Voltage), 1=HVP (High-Voltage, needed for Dyson BMS)
uint8_t progMode = 1;  // Default HVP for PIC16LF1847 Dyson BMS

// Manual VPP flag: when true, EnterHVPmode skips transistor control
// (user has already applied 8-9V to MCLR externally)
bool manualVPP = false;

#define DAT_INPUT   pinMode(PIN_DAT, INPUT);
#define DAT_OUTPUT  pinMode(PIN_DAT, OUTPUT);
#define DAT_LOW     digitalWrite(PIN_DAT,   LOW)
#define DAT_HIGH    digitalWrite(PIN_DAT,   HIGH)
#define DAT_GET     digitalRead(PIN_DAT)

#define CLK_INPUT   pinMode(PIN_CLK, INPUT);
#define CLK_OUTPUT  pinMode(PIN_CLK, OUTPUT);
#define CLK_LOW     digitalWrite(PIN_CLK,   LOW)
#define CLK_HIGH    digitalWrite(PIN_CLK,   HIGH)

// RESET pin controls NPN transistor: HIGH = transistor ON = MCLR pulled to GND
// This protects ESP32 from 8-9V VPP when using HVP mode
#define RESET_OUTPUT pinMode(PIN_RESET, OUTPUT);
void RESET_LOW(){   digitalWrite(PIN_RESET, LOW);}   // Transistor OFF, MCLR released
void RESET_HIGH(){  digitalWrite(PIN_RESET, HIGH);}  // Transistor ON, MCLR = GND

// VPP control via NPN shunt transistor on GPIO 14:
//   NPN collector → MCLR, emitter → GND, base ← GPIO 14 via 1K
//   VPP_ON:  GPIO LOW  → NPN OFF → 8-9V reaches MCLR
//   VPP_OFF: GPIO HIGH → NPN ON  → MCLR clamped to GND (VPP blocked)
#define VPP_OUTPUT  pinMode(PIN_VPP, OUTPUT);
void VPP_OFF(){    digitalWrite(PIN_VPP, HIGH);}   // NPN ON: clamp MCLR to GND
void VPP_ON(){     digitalWrite(PIN_VPP, LOW);}    // NPN OFF: 8-9V reaches MCLR


uint16_t currentAddress;


//
//
//
void PicSetup() {
  RESET_OUTPUT;
  VPP_OUTPUT;
  DAT_INPUT;
  CLK_INPUT;
  RESET_LOW();   // Transistor OFF, MCLR released (PIC can run)
  VPP_OFF();     // VPP disabled
}


//
// Prepare for manual HVP: pull MCLR low via NPN1, set PGD/PGC low.
// Called via /prep_hvp endpoint. User then connects 8-9V to MCLR
// (NPN1 holds it at GND). When /flash is called, EnterHVPmode releases
// NPN1 so MCLR rises to VPP and the PIC enters programming mode.
//
void PrepManualHVP() {
  DAT_OUTPUT;
  CLK_OUTPUT;
  CLK_LOW;
  DAT_LOW;
  delayMicroseconds(500);
  RESET_HIGH();    // NPN1 ON: pull MCLR to GND
  manualVPP = true;
  Serial.println("Manual HVP prep: MCLR held LOW, PGD/PGC low. Connect 8-9V to MCLR now.");
}


//
// Enter High-Voltage Programming mode (required when LVP is disabled)
// Sequence: Hold MCLR at GND, then apply VPP (8-9V) to MCLR
// The NPN transistor circuit allows safe VPP application
//
void EnterHVPmode() {
  DAT_OUTPUT;
  CLK_OUTPUT;
  CLK_LOW;
  DAT_LOW;
  delayMicroseconds(500);

  if (manualVPP) {
    // NPN1 is holding MCLR at GND, user has connected 8-9V.
    // Release NPN1 so MCLR rises to VPP → PIC enters HVP mode.
    Serial.println("HVP: Manual VPP — releasing MCLR (NPN1 OFF)");
    RESET_LOW();     // NPN1 OFF: release MCLR, VPP reaches pin
    delayMicroseconds(500); // T_PENTH: >100us hold time
    return;
  }

  RESET_HIGH();    // Transistor ON: MCLR = GND
  delayMicroseconds(500);
  VPP_ON();        // Apply 8-9V VPP (via auto-VPP circuit on GPIO14)
  RESET_LOW();     // Transistor OFF: release MCLR so VPP reaches pin
  delayMicroseconds(500); // T_PENTH: >100us hold time
}


//
// Send the magic 32 bits plus one extra clock to get into the LVP programming 
// mode as long as the RESET line is kept low.
//
void EnterLVPmode() {
  DAT_OUTPUT;
  CLK_OUTPUT;
  CLK_LOW;
  DAT_LOW;
  delayMicroseconds(500);
  RESET_HIGH();    // Transistor ON: MCLR = GND
  delayMicroseconds(500);
  Send(0b01010000,8); // P
  Send(0b01001000,8); // H
  Send(0b01000011,8); // C
  Send(0b01001101,8); // M
  Send(0,1);          // and one single final bit
  delayMicroseconds(DLY1);
 }


//
// Enter programming mode (selects HVP or LVP based on progMode setting)
//
void EnterProgMode() {
  if (progMode == 1) {
    Serial.println("Entering HVP mode...");
    EnterHVPmode();
  } else {
    Serial.println("Entering LVP mode...");
    EnterLVPmode();
  }
}


//
// Clock out data to the PIC. The "bits" argument
// specifies how many bits to be sent.
//
void Send(uint16_t data, uint8_t bits) {
  DAT_OUTPUT;
  delayMicroseconds(DLY1);
  for (uint8_t i=0; i<bits; i++) {
    if (data&0x01) {
      DAT_HIGH;
    } else {
      DAT_LOW;
    }
    delayMicroseconds(DLY1);
    CLK_HIGH;
    delayMicroseconds(DLY1);
    CLK_LOW;
    delayMicroseconds(DLY1);
    data = data >> 1;
  }
  DAT_LOW;
}


//
// Clock in 16 bits of data from the PIC
//
uint16_t Read16(void){
  uint16_t data=0;
  
  DAT_INPUT;
  delayMicroseconds(DLY1);
  for (uint8_t i=0; i<16; i++) {
    CLK_HIGH;
    delayMicroseconds(DLY1);
    CLK_LOW;
    delayMicroseconds(DLY1);
    data=data >> 1;
    if (DAT_GET) data = data | 0x8000;
  }
  return data;
}


//
// Send the RESET ADDRESS command to the PIC
//
void CmdResetAddress(void) {
  currentAddress=0;
  Send(0x16,6);
  delayMicroseconds(DLY2);
}

//
// Send the INCREMENT ADDRESS command to the PIC
//
void CmdIncAddress(void) {
  currentAddress++;
  Send(0x06,6);
  delayMicroseconds(DLY1);
}

//
// Send the BEGIN PROGRAMMING INTERNAL TIMED command to the PIC
//
void CmdBeginProgramI(void) {
  Send(0x08,6);
  delayMicroseconds(DLY2);
}

//
// Send the LOAD CONFIG command to the PIC
//
void CmdLoadConfig(uint16_t data) {
  currentAddress=0;
  Send(0x00,6);
  Send(data,16);
  delayMicroseconds(DLY2);
}


//
// Send the READ DATA command to the PIC
// TODO: This command should not increment the address here
//
void CmdReadData(uint16_t *data, uint8_t cnt) {
for (uint8_t i=0; i<cnt; i++) {
  Send(0x04, 6);
  delayMicroseconds(DLY1);
  data[i] = (Read16() & 0x7FFE) >> 1;
  CmdIncAddress();
  }
}


//
// Send the BULK ERASE command to the PIC
//
void CmdBulkErase(void) {
  CmdLoadConfig(0);
  Send(0x09,6);
  delay(10);
}



//
// Dump the contents of the PIC flash memory onto the...
//
void DumpMemory(void (*outFunc)(const String)) {
  char buf[120];
  bool isBlank;
  uint16_t data[16];
  
  CmdResetAddress();
  for (uint16_t rows=0; rows<FLASHSIZE/16; rows++) {
    CmdReadData(data,16);
    isBlank=true;
    for (uint8_t i=0; i<16; i++) {
      if (data[i]!=0x3FFF) isBlank=false; 
    }
    if (!isBlank) {
      uint16_t address=rows*16;
      sprintf(buf,"%04X: %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X<br />",
        address,
        data[0],data[1],data[2],data[3],
        data[4],data[5],data[6],data[7],
        data[8],data[9],data[10],data[11],
        data[12],data[13],data[14],data[15]
      );
      outFunc(String(buf));
    }
    yield();
  }  
}




//
// Dump the CONFIG memory parts of the PIC
//
void DumpConfig(void (*f)(const String)) {
  uint16_t data[16];
  char bin1[20];
  char bin2[20];
  char buf[1000];

  CmdLoadConfig(0x00);
  CmdReadData(data,16);

  ToBinary16(bin1,data[CONFIG1]);
  ToBinary16(bin2,data[CONFIG2]);
  sprintf(buf,
    "DEV ID : %04x<br/>"
    "DEV REV: %04x<br/>"
    "CONFIG1: %04x (%s)<br/>"
    "CONFIG2: %04x (%s)<br/>"
    "USER ID: %04x %04x %04x %04x<br/>",
    data[DEVID],
    data[DEVREV],
    data[CONFIG1], bin1,
    data[CONFIG2], bin2,
    data[USERID+0],data[USERID+1],data[USERID+2],data[USERID+3]
  );
  f(String(buf));
}


//
// Flash one word of data into the specified location on the PIC
//
void Store(uint32_t address, uint16_t data) {
  Serial.printf("Store(address:0x%08X, data:0x%04X\n",address,data);
  if (address<currentAddress) CmdResetAddress();
  while (address>currentAddress) CmdIncAddress();
  Send(0x02,6); 
  Send(data<<1,16); 
  CmdBeginProgramI();
  delay(3);
}




//
//
//
bool PicFlash(String uploadFilename) {
    int num=0;
    int cnt=0;
    
    // First pass: parse hex file to extract config words
    uint16_t userIds[4] = {PICBLANKWORD, PICBLANKWORD, PICBLANKWORD, PICBLANKWORD};
    uint16_t config1 = PICBLANKWORD;
    uint16_t config2 = PICBLANKWORD;
    bool hasConfig = false;
    
    Serial.print("Parsing hex file: ");
    Serial.println(uploadFilename);
    File fp = SPIFFS.open(uploadFilename, "r");
    if (!fp) {
      webSocket.sendTXT(num, "pFile error");
      Serial.print("Couldn't open ");
      Serial.println(uploadFilename);
      return false;
    }
    
    uint16_t parseOffset = 0;
    while(fp.available()) {
      String s = fp.readStringUntil('\n');
      uint8_t d_len = HexDec2(s[1],s[2]);
      uint16_t d_addr = HexDec4(s[3],s[4],s[5],s[6]);
      uint8_t d_typ = HexDec2(s[7],s[8]);
      
      if (d_typ==0x04) {
        parseOffset = HexDec4(s[11],s[12],s[9],s[10]);
      }
      if (d_typ==0x00 && parseOffset != 0) {
        // Config/User ID space data
        for (uint8_t i=0; i<d_len*2; i+=4) {
          uint32_t wordAddr = ((uint32_t)parseOffset << 16 | d_addr) / 2 + i/4;
          uint16_t data = HexDec4(s[11+i],s[12+i],s[9+i],s[10+i]);
          // Map to config space addresses (0x8000+)
          if (wordAddr >= 0x8000 && wordAddr <= 0x8003) {
            userIds[wordAddr - 0x8000] = data;
            hasConfig = true;
          }
          if (wordAddr == 0x8007) { config1 = data; hasConfig = true; }
          if (wordAddr == 0x8008) { config2 = data; hasConfig = true; }
        }
      }
    }
    fp.close();
    
    if (hasConfig) {
      Serial.printf("Config from hex: CONFIG1=0x%04X CONFIG2=0x%04X\n", config1, config2);
      Serial.printf("User IDs: %04X %04X %04X %04X\n", userIds[0], userIds[1], userIds[2], userIds[3]);
    } else {
      Serial.println("No config words found in hex file, using defaults");
    }
    
    // Enter programming mode with retry loop
    uint16_t devId = 0x3FFF;
    bool isManual = manualVPP;  // Save flag — ExitProgMode clears it
    for (int attempt = 1; attempt <= 10; attempt++) {
      Serial.printf("HVP entry attempt %d/10...\n", attempt);
      char attemptMsg[50];
      sprintf(attemptMsg, "pHVP entry attempt %d/10...", attempt);
      webSocket.sendTXT(num, attemptMsg);

      manualVPP = isManual;  // Restore flag for this attempt
      EnterProgMode();
      CmdResetAddress();
      CmdLoadConfig(0x00);
      uint16_t devCheck[16];
      CmdReadData(devCheck, 16);
      devId = devCheck[DEVID];
      Serial.printf("  DEVID: 0x%04X\n", devId);

      if (devId != 0x3FFF && devId != 0x0000) {
        break;  // PIC responded
      }

      // Failed — exit prog mode, wait, then retry
      ExitProgMode();
      Serial.println("  PIC not responding, retrying...");
      delay(500);

      // Re-prep for manual VPP (NPN1 pulls MCLR low again before next attempt)
      if (isManual) {
        PrepManualHVP();
        delay(200);
      }
    }

    if (devId == 0x3FFF || devId == 0x0000) {
      Serial.println("ERROR: PIC not responding after 10 attempts. HVP entry failed!");
      webSocket.sendTXT(num, "pERROR: PIC not responding after 10 attempts! Check wiring/VPP.");
      ExitProgMode();
      return false;
    }

    Serial.printf("PIC detected! DEVID=0x%04X\n", devId);
    char devMsg[60];
    sprintf(devMsg, "pPIC detected (DEVID=0x%04X). Erasing...", devId);
    webSocket.sendTXT(num, devMsg);

    CmdResetAddress();
    CmdLoadConfig(0x00);
    CmdBulkErase();
    
    // Write User IDs and Config words (from hex file or defaults)
    if (hasConfig) {
      Store(USERID+0, userIds[0]); delay(5);
      Store(USERID+1, userIds[1]); delay(5);
      Store(USERID+2, userIds[2]); delay(5);
      Store(USERID+3, userIds[3]); delay(5);
      Store(CONFIG1, config1); delay(5);
      Store(CONFIG2, config2); delay(5);
    }

    // Second pass: flash program memory from hex file
    Serial.print("Flashing code from ");
    Serial.println(uploadFilename);
    File f = SPIFFS.open(uploadFilename, "r");
    if (!f) {
      webSocket.sendTXT(num, "pFile error");
      Serial.print("Couldn't open ");
      Serial.println(uploadFilename);
      return false;
    }
    webSocket.sendTXT(num, "pFlashing...");
    uint16_t offset = 0;
    uint16_t lastProgress = 0;
    while(f.available()) {
      uint8_t d_len;
      uint16_t d_addr;
      uint8_t d_typ;
      String s = f.readStringUntil('\n');
      d_len=HexDec2(s[1],s[2]);
      d_addr=HexDec4(s[3],s[4],s[5],s[6]);
      d_typ=HexDec2(s[7],s[8]);
      if (d_typ==0x00) {
        for (uint8_t i=0; i<d_len*2; i+=4) {
          uint32_t address=d_addr/2+i/4;
          uint16_t data=HexDec4(s[11+i],s[12+i],s[9+i],s[10+i]);
          if (offset==0) {
            cnt++;
            Store(address,data);
            // Progress every 100 words
            if (cnt - lastProgress >= 100) {
              lastProgress = cnt;
              Serial.printf("Flashed %d words...\n", cnt);
              char pmsg[40];
              sprintf(pmsg, "pFlashed %d words...", cnt);
              webSocket.sendTXT(num, pmsg);
            }
          }
        }
      }
      if (d_typ==0x04) {
        offset=HexDec4(s[11],s[12],s[9],s[10]);
        Serial.printf("Offset=%04x\n",offset);
      }
    }
    f.close();
    char tmps[40];
    sprintf(tmps,"pFlashed %d words successfully",cnt);
    webSocket.sendTXT(num, tmps);
    Serial.println(tmps);

    ExitProgMode();
    return true;
}


//
// Cleanly exit programming mode
//
void ExitProgMode() {
    if (!manualVPP) {
      VPP_OFF();       // Remove VPP (auto-VPP circuit)
    }
    RESET_HIGH();      // NPN1 ON: pull MCLR to GND (exit programming)
    delay(1);
    RESET_LOW();       // NPN1 OFF: release MCLR
    if (manualVPP) {
      Serial.println("Manual VPP: Remove 8-9V from MCLR now!");
    }
    manualVPP = false;  // Reset for next operation
    DAT_INPUT;
    CLK_INPUT;
    delay(10);       // Let PIC reset and start running
}


//
//
//
void PicReadConfigs(void (*f)(const String)) {
    EnterProgMode();
    CmdResetAddress();
    DumpConfig(f);
    f("<br />");
    DumpMemory(f);
    ExitProgMode();
}


//
//
//
void PicReset(char type) {
  if (type=='H') RESET_LOW();   // Release MCLR (PIC runs)
  if (type=='L') RESET_HIGH();  // Pull MCLR low (PIC halted)
  if (type=='P') {
      RESET_HIGH();  // Pull MCLR low
      delay(250);
      RESET_LOW();   // Release MCLR (PIC starts)
  }
}

