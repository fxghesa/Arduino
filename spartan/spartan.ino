#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <time.h>

#pragma region SSID
#include <ESP8266WiFi.h>
const char* ssid = "Brawijaya";
const char* password = "ujungberung";
// const char* ssid = "";
// const char* password = "sandydimas17";
#pragma endregion

#pragma region Cloud Firestore
#include <Firebase_ESP_Client.h>
#define API_KEY "AIzaSyBbOZqg19S67nkfzis-7SXGvaQzN_GPGks"
#define FIREBASE_PROJECT_ID "apps-2ee38"
#define USER_EMAIL "ghesa@gmail.com"
#define USER_PASSWORD "123qwe"
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
#pragma endregion

#pragma region Global Variable
bool qcMode = true;
#pragma endregion

#pragma region DS18B20 Temperature Sensor
#include <OneWire.h>
#include <DallasTemperature.h>
// Data wire is plugged into digital pin 2 on the Arduino
#define ONE_WIRE_BUS 5
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
#pragma endregion

#pragma region Item Document
#define ITEM_0 "ZrM6BzaBriaNSliSGlXF"
#define ITEM_1 "XQBI3RT7LEPIDcVh6XWR"
#define ITEM_2 "BnF9PZs02yHXBTBdwszT"
#define ITEM_3 "XE584ON3H6cEE2Nn9btP"
#define ITEM_4 "lNYFTm6MssOC1ZtFSnZD"
#define ITEM_0_QC "ZQtO1TTwNsO2ilsLkQAx"
#define ITEM_1_QC "oriYYaxGGzZJof8TrZER"
#pragma endregion

void setup() {
  #pragma region Init Serial, WiFi, and Cloud Firestore
  Serial.begin(9600);
  sensors.begin();
  WiFi.begin(ssid, password);
  startWifiConnection();
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  fbdo.setBSSLBufferSize(2048, 2048);
  fbdo.setResponseSize(2048);
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  #pragma endregion

  // one time test
  // main();
}

void loop() {
  // prod mode
  main();
}

int main() {
  sensors.requestTemperatures();
  float currentReadTemperature = sensors.getTempCByIndex(0);
  if (WiFi.status() == WL_CONNECTED) {
    int itemCode = 0;
    if (Firebase.ready()) {
      redo:
      //"2023-02-26T17:00:00.00000Z"; // ISO 8601/RFC3339 UTC "Zulu" format
      String currentTime = getTimeStampNow() + "Z";
      if (insertLog(itemCode, currentReadTemperature, currentTime)) {
        if (updateItemHeader(itemCode, currentReadTemperature)) {
          Serial.println("success!");
        } else {
          Serial.println("header failed!");
          goto redo;
        }
      } else {
        Serial.println("log failed!");
        goto redo;
      }
    }
  } else {
    startWifiConnection();
  }
  delay(10000);
}

bool updateItemHeader(int itemCode, float currentReadTemperature) {
  String itemDocument = getDocumentCode(itemCode);
  String documentPath = qcMode ? "ITEMHEADERQC" : "ITEMHEADER";
  documentPath += "/" + itemDocument;
  FirebaseJson content;
  content.set("fields/TemperatureValue/doubleValue", String(currentReadTemperature).c_str());
  if (Firebase.Firestore.patchDocument(
      &fbdo, 
      FIREBASE_PROJECT_ID, 
      "", 
      documentPath.c_str(),
      content.raw(), 
      "TemperatureValue")) {
    // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    return true;
  } else {
    Serial.println(fbdo.errorReason());
    return false;
  }
}

bool insertLog(int itemCode, float currentReadTemperature, String dateNow) {
  String documentPath = qcMode ? "ITEMTEMPERATUREQC" : "ITEMTEMPERATURE";
  FirebaseJson content;
  content.set("fields/ItemCode/doubleValue", String(itemCode).c_str());
  content.set("fields/TemperatureValue/doubleValue", String(currentReadTemperature).c_str());
  content.set("fields/LastUpdateDate/timestampValue", String(dateNow).c_str());
  content.set("fields/CreateBy/stringValue", "system");
  if(Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
    // Serial.printf("ok\n%s\n", fbdo.payload().c_str());
    return true;
  }
  else {
    Serial.println(fbdo.errorReason());
    return false;
  }
}

String getTimeStampNow() {
  unsigned long randTime = millis();
  String serverTimePath = "SERVERTIME/ServerTime";
  FirebaseJson content;
  content.set("fields/CreateBy/stringValue", String(randTime).c_str());
  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str(), content.raw(), "CreateBy")) {
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str())) {
      // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      DynamicJsonDocument doc(500);
      DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());
      if (error) {
        Serial.println("deserializeJson() failed: ");
        Serial.println(error.f_str());
        return "";
      }
      const char* timeResult = doc["updateTime"];
      String date = convertDateTime(timeResult);
      return date;
    } else {
      Serial.println(fbdo.errorReason());
      return "";
    }
  } else {
    Serial.println(fbdo.errorReason());
    return "";
  }
}

void startWifiConnection() {
  #pragma region Reconnect WiFi
  Serial.printf("Connecting ");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected with IP: ");
  Serial.println(WiFi.localIP());
  #pragma endregion
}

String getDocumentCode(int itemCode) {
  if (!qcMode) {
    switch (itemCode) {
      default:
      case 0:
        return ITEM_0;
      case 1:
        return ITEM_1;
      case 2:
        return ITEM_2;
      case 3:
        return ITEM_3;
      case 4:
        return ITEM_4;
    }
  } else {
    switch (itemCode) {
      default:
      case 0:
        return ITEM_0_QC;
      case 1:
        return ITEM_1_QC;
    }
  }
}

String convertDateTime(const char* date) {
  //https://arduino.stackexchange.com/questions/83860/esp8266-iso-8601-string-to-tm-struct
  struct tm tm = {0};
  char buf[100];
  // Convert to tm struct
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  // Can convert to any other format
  // strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &tm); //original
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  // Serial.printf("%s", buf);
  return String(buf);
}
