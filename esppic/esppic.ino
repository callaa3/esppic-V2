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
    PicFlash(uploadFilename);
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
    PicFlash(uploadFilename);
    char *html=(char *)malloc(10000);
    strcpy(html,"<h1>FLASH DONE</h1><a href=\"/\">Back</a>");
    server.send(200, "text/html", html);
    free(html);
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

