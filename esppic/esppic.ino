//
// esppic-V2 - An ESP32-based Microchip PIC programmer
//-----------------------------------------------------
// Forked from github.com/SmallRoomLabs/esppic
// Updated for ESP32 (ESP-WROOM-32) and PIC16LF1847 (Dyson BMS)
//
// Original Copyright (c) 2016 Mats Engstrom SmallRoomLabs
// Released under the MIT license
//
// Fixes applied:
//   - Issue #2: Renamed conflicting 'filename' variable (PR #4)
//   - Issue #3: Fixed handleFileRead blocking page access (PR #5)
//   - Ported from ESP8266 to ESP32
//   - Added HVP (High-Voltage Programming) support for PIC16LF1847
//   - Config words read from hex file instead of hardcoded
//

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>

#include "pic_defs.h"

#define SWAP16(x) (((x & 0x00ff) << 8) | ((x & 0xff00) >> 8))

#include "index_html.h"
#include "upload_html.h"
#include "favicon_ico.h"
#include "logo_png.h"

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
String uploadFilename;
File fsUploadFile;
char resetflag=0;
uint8_t wsNum;

// Forward declarations for symbols defined in prg_pic.ino
extern bool manualVPP;
void PrepManualHVP();
bool PicFlash(String uploadFilename);

//
//
//
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  IPAddress ip;
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      wsNum=num;
      ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      webSocket.sendTXT(num, "Connected");
      webSocket.sendTXT(num, "f");
      webSocket.sendTXT(num, "s");
      webSocket.sendTXT(num, "p...");
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if (payload[0]=='R') resetflag=payload[1];
      if (payload[0]=='F' && payload[1]=='L') {
        Serial.println("[flash]");
        PicFlash(uploadFilename);
      }
      break;
    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\n", num, length);
      for (size_t i = 0; i < length; i++) Serial.printf("%02X ", payload[i]);
      Serial.println();
      break;
  }
}



//
//
//
void handleFileUpload() {
  char tmps[30];
  static uint32_t bytesSoFar=0;
  if(server.uri() != "/upload") return;
  Serial.println("handleFileUpload()"); 
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    bytesSoFar=0;
    uploadFilename = upload.filename;
    sprintf(tmps,"f%s",uploadFilename.c_str());
    webSocket.sendTXT(wsNum, tmps);
    sprintf(tmps,"s%d/%d bytes uploaded",bytesSoFar,upload.totalSize);
    webSocket.sendTXT(wsNum, tmps);
    webSocket.sendTXT(wsNum, "pUploading file");
    if (uploadFilename.endsWith(".hex")) uploadFilename = "/hex/"+uploadFilename;
    else uploadFilename="/"+uploadFilename;
    Serial.printf("Receiving file %s\n",uploadFilename.c_str()); 
    fsUploadFile = SPIFFS.open(uploadFilename, "w");
  }
  else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
      bytesSoFar+=upload.currentSize;
      sprintf(tmps,"s%d bytes uploaded",bytesSoFar);
      webSocket.sendTXT(wsNum, tmps);
    }
  }
  else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
    fsUploadFile.close();
    Serial.println("Success");
    webSocket.sendTXT(wsNum, "pFile uploaded" );
    delay(250);
    if (!manualVPP) {
      PicFlash(uploadFilename);
    } else {
      Serial.println("Manual VPP mode: skipping auto-flash after upload. Use /flash to program.");
      webSocket.sendTXT(wsNum, "pManual VPP: upload done, use /flash when VPP applied");
    }
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.printf("handleFileRead(%s)\n",path.c_str());
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  if(SPIFFS.exists(path)){
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    Serial.printf("%s (%d bytes) served\n", path.c_str(), sent);
    return true;
  }
  return false;
}



// Trampoline function
void funcSendContent(String s) {
  server.sendContent(String(s));
}

//
// Setup pins and start the serial comms for debugging
//
void setup() {
  PicSetup();

  Serial.begin(115200);
  Serial.println("\n\n\nESPPIC v2.0 starting (ESP32 + PIC16LF1847)\n");
  if(!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  Serial.printf("%d of %d bytes used on SPIFFS\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
  ConnectToWifi();

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
    
  server.on("/i.html", HTTP_GET, []() {
    Serial.println("HTTP_GET /");
    server.send_P(200,"text/html", index_html);
  });

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/html", "<a href=\"/\"><- BACK TO INDEX</a><br><br>Upload Successful!<br><br><a href=\"/listpayloads\">List Payloads</a>");
  },handleFileUpload);

  server.on("/favicon.ico", HTTP_GET, []() {
    Serial.println("HTTP_GET /favicon.ico");
    server.send_P(200, "image/x-icon", favicon_ico, sizeof(favicon_ico));
  });

  server.on("/logo.png", HTTP_GET, []() {
    Serial.println("HTTP_GET /logo.png");
    server.sendHeader("Cache-Control", "public,max-age:86400");
    server.send_P(200, "image/png", logo_png, sizeof(logo_png));
  });

  server.on("/fm", HTTP_GET, []() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html","");
    server.sendContent(String("<h1>File Manager</h1><samp>"));
  
    // Delete is done before directory listing
    if (server.args()>0) {
      if (server.argName(0)=="d") {
        SPIFFS.remove(server.arg(0));
        server.sendContent("<h3>Deleted "+server.arg(0)+"</h3>");
      }
    }

    File root = SPIFFS.open("/");
    File dir = root.openNextFile();
    while (dir) {
      String fname = String(dir.name());
      String str="<a href='/fm?d="+fname+"'>delete</a> ";
      str += "<a href='/fm?p="+fname+"'>show</a> ";
      str += fname;
      str += " (";
      str += dir.size();
      str += " bytes)<br>";
      server.sendContent(str);
      dir = root.openNextFile();
    }

    server.sendContent(String("</samp>"));

    // Printing the contents of file is done after directory listing
    if (server.args()>0) {
      if (server.argName(0)=="p") {
        server.sendContent("<h3>"+server.arg(0)+"</h3>");
        String tag="pre";
        String contentType="";
        server.sendContent("<"+tag+">");
        File file = SPIFFS.open(server.arg(0), "r");
        server.streamFile(file, contentType);
        file.close();
        server.sendContent("</"+tag+">");
      }
    }

    server.client().stop();
  });

  server.on("/readconfigs", HTTP_GET, []() {
    Serial.println("HTTP_GET /readconfig");
    char tmps[1000];
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html","");
    server.sendContent(String("<h1>MEMORY DUMP</h1><samp>"));
    PicReadConfigs(funcSendContent);
    server.sendContent(String("</samp>"));
    server.client().stop();
  });


  server.on("/flash", HTTP_GET, []() {
    Serial.println("HTTP_GET /flash");
    bool ok = PicFlash(uploadFilename);
    if (ok) {
      server.send(200, "text/html", "<h1>FLASH DONE</h1><a href=\"/\">Back</a>");
    } else {
      server.send(200, "text/html", "<h1>FLASH FAILED</h1><p>PIC not responding. Check wiring and VPP.</p><a href=\"/\">Back</a>");
    }
  });

  server.on("/prep_hvp", HTTP_GET, []() {
    Serial.println("HTTP_GET /prep_hvp");
    PrepManualHVP();
    server.send(200, "text/plain", "OK");
  });

  server.on("/diag", HTTP_GET, []() {
    Serial.println("HTTP_GET /diag");
    String result = "ESPPIC-V2 Wiring Diagnostics\n\n";
    char buf[100];

    // Test MCLR control (NPN1 on GPIO 27)
    result += "1. MCLR transistor (GPIO 27):\n";
    RESET_OUTPUT;
    RESET_HIGH();   // NPN1 ON: MCLR pulled to GND
    delay(50);
    result += "   GPIO 27 = HIGH (NPN1 ON, MCLR should be at GND)\n";

    // Test VPP control (NPN2 on GPIO 14)
    result += "\n2. VPP transistor (GPIO 14):\n";
    VPP_OUTPUT;
    VPP_ON();       // NPN2 OFF: 8-9V reaches MCLR
    delay(50);
    result += "   VPP_ON: GPIO 14 = LOW (NPN2 OFF, 8-9V should reach MCLR)\n";

    // Now release MCLR and try to read DEVID
    RESET_LOW();    // NPN1 OFF: release MCLR so VPP reaches pin
    delay(50);
    result += "\n3. Attempting HVP entry (MCLR released, VPP ON)...\n";

    // Set data/clock low
    DAT_OUTPUT;
    CLK_OUTPUT;
    DAT_LOW;
    CLK_LOW;
    digitalWrite(PIN_CLK, LOW);
    delayMicroseconds(500);

    // Try reading DEVID
    CmdResetAddress();
    CmdLoadConfig(0x00);
    uint16_t data[16];
    CmdReadData(data, 16);

    char buf[80];
    sprintf(buf, "   DEVID  = 0x%04X  (expect 0x1480 for PIC16LF1847)\n", data[DEVID]);
    result += buf;
    sprintf(buf, "   DEVREV = 0x%04X\n", data[DEVREV]);
    result += buf;
    sprintf(buf, "   CONFIG1= 0x%04X\n", data[CONFIG1]);
    result += buf;
    sprintf(buf, "   CONFIG2= 0x%04X\n", data[CONFIG2]);
    result += buf;

    if (data[DEVID] == 0x3FFF) {
      result += "\n   RESULT: FAIL - PIC not responding.\n";
      result += "   Check:\n";
      result += "   - NPN1 collector is connected to MCLR/VPP (Pin 4)\n";
      result += "   - NPN1 emitter is connected to GND\n";
      result += "   - NPN1 base has 1K resistor to GPIO 27\n";
      result += "   - 8-9V is connected to MCLR/VPP (or NPN2 circuit)\n";
      result += "   - ICSPDAT (GPIO 25) goes to Pin 13 (RB7/PGD)\n";
      result += "   - ICSPCLK (GPIO 26) goes to Pin 12 (RB6/PGC)\n";
      result += "   - GND is shared between ESP32, BMS, and 8-9V supply\n";
      result += "   - Battery is awake (press trigger for V6)\n";
    } else {
      result += "\n   RESULT: OK - PIC is responding!\n";
    }

    // Clean up
    VPP_OFF();
    RESET_LOW();
    DAT_INPUT;
    CLK_INPUT;
    delay(10);

    server.send(200, "text/plain", result);
  });

  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });  
}



//
// Just loop the webserver & websocket handlers continously...
//
void loop(void) {
  server.handleClient();
  webSocket.loop();
  delay(10);
  if (resetflag) {
    if (resetflag=='H') PicReset('H');
    if (resetflag=='L') PicReset('L');
    if (resetflag=='P') PicReset('P');
    resetflag=0;
  }
}

