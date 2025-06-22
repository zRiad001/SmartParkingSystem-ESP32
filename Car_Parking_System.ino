#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h> 
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

const char* ssid = "S23"; 
const char* password = "abcd0000"; 

const int IR_SPOT_PINS[] = {15, 13, 18, 19, 12}; 
const int NUM_PARKING_SPOTS = 5;
const int IR_ENTRY_PIN = 4; 
const int IR_EXIT_PIN = 32; 
const int SDA_PIN = 21; 
const int SCL_PIN = 22; 
const int SERVO_ENTRY_PIN = 2;
const int SERVO_EXIT_PIN = 33; 

const int LCD_ADDRESS = 0x27; 
const int LCD_COLS = 16;
const int LCD_ROWS = 2;

const int SERVO_CLOSED_ANGLE = 0; 
const int SERVO_OPEN_ANGLE = 90; 

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
Servo entryServo; 
Servo exitServo;  
AsyncWebServer server(80);

int irSpotStates[NUM_PARKING_SPOTS];
int irEntryState = 0;
int irExitState = 0;
int prevIrSpotStates[NUM_PARKING_SPOTS];
int prevIrEntryState = HIGH;
int prevIrExitState = HIGH;
int freeSpotsCount = 0;
const int SENSOR_OCCUPIED_STATE = LOW;
const int SENSOR_FREE_STATE = HIGH;

int reservedSpotIndex = -1; 
unsigned long reservationStartTime = 0;
const unsigned long RESERVATION_TIMEOUT_MS = 5 * 60 * 1000; 

String logBuffer = ""; 
const int MAX_LOG_LENGTH = 500; 

String webRequestLcdMessage = "";
unsigned long webRequestLcdMessageTimeout = 0; 
const unsigned long WEB_REQUEST_LCD_DURATION = 2000; 

String sensorLcdMessage = ""; 
unsigned long sensorLcdMessageTimeout = 0; 
const unsigned long SENSOR_MESSAGE_DURATION = 3000;
IPAddress local_IP(192, 168, 212, 230);
IPAddress gateway(192, 168, 212, 1);
IPAddress subnet(255, 255, 255, 0);

String getTimestamp() {
  unsigned long currentMillis = millis();
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  char timeString[9];
  sprintf(timeString, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timeString);
}

void logEvent(String event) {
  String timestamp = getTimestamp();
  String logEntry = "[" + timestamp + "] " + event + "\n";
  Serial.print(logEntry);
  logBuffer = logEntry + logBuffer;
  if (logBuffer.length() > MAX_LOG_LENGTH) {
    logBuffer = logBuffer.substring(0, MAX_LOG_LENGTH);
    int lastNewline = logBuffer.lastIndexOf('\n');
    if (lastNewline != -1) {
      logBuffer = logBuffer.substring(0, lastNewline + 1);
    }
  }
}

void setWebRequestLcdMessage(String url) {
  webRequestLcdMessage = "Web: " + url.substring(0, LCD_COLS - 5);
  if (url.length() > LCD_COLS - 5) {
    webRequestLcdMessage += "..";
  }
  webRequestLcdMessageTimeout = millis() + WEB_REQUEST_LCD_DURATION;
  logEvent("HTTP Request: " + url);
}

void setSensorLcdMessage(String message) {
  sensorLcdMessage = message;
  sensorLcdMessageTimeout = millis() + SENSOR_MESSAGE_DURATION;
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  logEvent("ESP32 Smart Parking System Starting...");

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Statik IP təyin etmək alınmadı!");
  }

  WiFi.begin(ssid, password);
  int connectAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && connectAttempts < 30) {
    delay(500);
    Serial.print(".");
    connectAttempts++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    logEvent("WiFi Connected! IP: " + WiFi.localIP().toString());
  } else {
    logEvent("WiFi Connection Failed!");
  }

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("IP:");
    lcd.print(WiFi.localIP());
  } else {
    lcd.print("WiFi Failed!");
  }
  delay(4000);
  lcd.clear();

  for (int i = 0; i < NUM_PARKING_SPOTS; i++) {
    pinMode(IR_SPOT_PINS[i], INPUT);
    prevIrSpotStates[i] = digitalRead(IR_SPOT_PINS[i]);
  }
  pinMode(IR_ENTRY_PIN, INPUT);
  pinMode(IR_EXIT_PIN, INPUT);

  entryServo.attach(SERVO_ENTRY_PIN);
  entryServo.write(SERVO_CLOSED_ANGLE);
  exitServo.attach(SERVO_EXIT_PIN);
  exitServo.write(SERVO_CLOSED_ANGLE);
  delay(1000);

  
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    String status = "{";
    status += "\"freeSpots\": " + String(freeSpotsCount) + ",";
    status += "\"totalSpots\": " + String(NUM_PARKING_SPOTS) + ",";
    status += "\"reservedSpotIndex\": " + String(reservedSpotIndex) + ",";
    status += "\"spots\": [";
    for (int i = 0; i < NUM_PARKING_SPOTS; i++) {
      int spotStatus;
      if (irSpotStates[i] == SENSOR_OCCUPIED_STATE) {
        spotStatus = 0; 
      } else {
        if (i == reservedSpotIndex) {
          spotStatus = 1; 
        } else {
          spotStatus = 2; 
        }
      }
      status += String(spotStatus);
      if (i < NUM_PARKING_SPOTS - 1) status += ",";
    }
    status += "],";
    status += "\"barrierEntrySensor\": " + String(irEntryState == SENSOR_OCCUPIED_STATE ? "true" : "false") + ",";
    status += "\"barrierExitSensor\": " + String(irExitState == SENSOR_OCCUPIED_STATE ? "true" : "false") + ",";

    status += "\"barrierEntryOpen\": " + String(entryServo.read() == SERVO_OPEN_ANGLE ? 1 : 0) + ",";
    status += "\"barrierExitOpen\": " + String(exitServo.read() == SERVO_OPEN_ANGLE ? 1 : 0) + ",";

    String escapedLog = logBuffer;
    escapedLog.replace("\\", "\\\\");
    escapedLog.replace("\"", "\\\"");
    escapedLog.replace("\r", "\\r");
    escapedLog.replace("\n", "\\n");
    status += "\"log\": \"" + escapedLog + "\"";

    status += "}";
    request->send(200, "application/json", status);
  });

  server.on("/reserve", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    String response = "";
    if (request->hasParam("spot")) {
      int spotNumber = request->getParam("spot")->value().toInt();
      if (spotNumber >= 1 && spotNumber <= NUM_PARKING_SPOTS) {
        int spotIndex = spotNumber - 1;
        if (irSpotStates[spotIndex] == SENSOR_FREE_STATE) {
          if (reservedSpotIndex != -1 && reservedSpotIndex != spotIndex) {
            logEvent("Spot " + String(reservedSpotIndex + 1) + " overridden by spot " + String(spotNumber));
          }
          reservedSpotIndex = spotIndex;
          reservationStartTime = millis();
          response = "Spot " + String(spotNumber) + " reserved.";
          logEvent(response);
          request->send(200, "text/plain", response);
        } else {
          response = "Spot " + String(spotNumber) + " is not free.";
          logEvent(response);
          request->send(400, "text/plain", response);
        }
      } else {
        response = "Invalid spot number.";
        logEvent(response);
        request->send(400, "text/plain", response);
      }
    } else {
      response = "Missing 'spot' parameter.";
      logEvent(response);
      request->send(400, "text/plain", response);
    }
  });

  server.on("/cancel_reserve", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    if (reservedSpotIndex != -1) {
      logEvent("Reservation for spot " + String(reservedSpotIndex + 1) + " cancelled.");
      reservedSpotIndex = -1;
    } else {
      logEvent("Cancel called, but no reservation existed.");
    }
    request->send(200, "text/plain", "Reservation cancelled.");
  });

  
  server.on("/open_entry", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    entryServo.write(SERVO_OPEN_ANGLE);
    logEvent("Manual command: Open entry barrier");
    request->send(200, "text/plain", "Entry barrier opened.");
  });

  server.on("/close_entry", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    entryServo.write(SERVO_CLOSED_ANGLE);
    logEvent("Manual command: Close entry barrier");
    request->send(200, "text/plain", "Entry barrier closed.");
  });

  server.on("/open_exit", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    exitServo.write(SERVO_OPEN_ANGLE);
    logEvent("Manual command: Open exit barrier");
    request->send(200, "text/plain", "Exit barrier opened.");
  });

  server.on("/close_exit", HTTP_GET, [](AsyncWebServerRequest *request) {
    setWebRequestLcdMessage(request->url());
    exitServo.write(SERVO_CLOSED_ANGLE);
    logEvent("Manual command: Close exit barrier");
    request->send(200, "text/plain", "Exit barrier closed.");
  });

  server.begin();
  logEvent("HTTP Server Started");
}

void loop() {
  
  for (int i = 0; i < NUM_PARKING_SPOTS; i++) {
    irSpotStates[i] = digitalRead(IR_SPOT_PINS[i]);
    if (irSpotStates[i] != prevIrSpotStates[i]) {
      String status = (irSpotStates[i] == SENSOR_OCCUPIED_STATE) ? "occupied" : "free";
      logEvent("Spot " + String(i + 1) + " " + status);
      prevIrSpotStates[i] = irSpotStates[i];
      
      if (i == reservedSpotIndex && irSpotStates[i] == SENSOR_OCCUPIED_STATE) {
         logEvent("Reserved spot " + String(reservedSpotIndex + 1) + " occupied. Reservation cancelled.");
         reservedSpotIndex = -1;
      }
    }
  }

  
  irEntryState = digitalRead(IR_ENTRY_PIN);
  if (irEntryState != prevIrEntryState) {
    if (irEntryState == SENSOR_OCCUPIED_STATE) {
      logEvent("Car detected at entry");
      setSensorLcdMessage("Welcome");

     
      entryServo.write(SERVO_OPEN_ANGLE);
      logEvent("Entry barrier opened by sensor");
    } else { 
      entryServo.write(SERVO_CLOSED_ANGLE);
      logEvent("Entry barrier closed by sensor");
    }
    prevIrEntryState = irEntryState;
  }

 
  irExitState = digitalRead(IR_EXIT_PIN);
  if (irExitState != prevIrExitState) {
    if (irExitState == SENSOR_OCCUPIED_STATE) {
      logEvent("Car detected at exit");
      setSensorLcdMessage("See you soon");

      exitServo.write(SERVO_OPEN_ANGLE);
      logEvent("Exit barrier opened by sensor");
    } else { 
      exitServo.write(SERVO_CLOSED_ANGLE);
      logEvent("Exit barrier closed by sensor");
    }
    prevIrExitState = irExitState;
  }


  
  freeSpotsCount = 0;
  for (int i = 0; i < NUM_PARKING_SPOTS; i++) {
    if (irSpotStates[i] == SENSOR_FREE_STATE) {
      freeSpotsCount++;
    }
  }

 
  if (reservedSpotIndex != -1) {
    if (millis() - reservationStartTime > RESERVATION_TIMEOUT_MS) {
      logEvent("Reservation timeout for spot " + String(reservedSpotIndex + 1));
      reservedSpotIndex = -1;
    }
  }

 
  lcd.clear();
  if (webRequestLcdMessage != "" && millis() < webRequestLcdMessageTimeout) {
    lcd.setCursor(0, 0);
    lcd.print("ESP32 SmartPark");
    lcd.setCursor(0, 1);
    lcd.print(webRequestLcdMessage);
  } else if (sensorLcdMessage != "" && millis() < sensorLcdMessageTimeout) {
    lcd.setCursor(0, 0);
    lcd.print("ESP32 SmartPark");
    lcd.setCursor(0, 1);
    lcd.print(sensorLcdMessage);
  }
  else {
    lcd.setCursor(0, 0);
    lcd.print("Free: ");
    lcd.print(freeSpotsCount);
    lcd.print("/");
    lcd.print(NUM_PARKING_SPOTS);

    lcd.setCursor(0, 1);
    if (reservedSpotIndex != -1) {
      lcd.print("Reserved: ");
      lcd.print(reservedSpotIndex + 1);
    } else {
      lcd.print("No reservations");
    }
  }

  delay(100);
}
