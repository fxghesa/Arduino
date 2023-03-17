#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <time.h>
int currentTime = 0; // can be minute or hour or day
//"2023-02-26T17:00:00.00000Z"; // ISO 8601/RFC3339 UTC "Zulu" format
int itemCode = 0;
String currentDate = "2023-03-01T00:00:00.00000Z";
int errorCount = 0;
void(* resetFunc) (void) = 0;

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
#define ITEM_5 "TqMR7gF6MC5i9IeFdTfB"
#define ITEM_0_QC "ZQtO1TTwNsO2ilsLkQAx"
#define ITEM_1_QC "oriYYaxGGzZJof8TrZER"
#pragma endregion

void setup() {
  Serial.println("[info] booting");
  #pragma region Init Serial, Sesor, WiFi, and Cloud Firestore
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

  // run main() one time test
  // main();
}

void loop() {
  // run main() loop mode
  main();
}

int main() {
  delay(30000); // 30 seconds
  // delay(60000); // 1 minute
  sensors.requestTemperatures();
  float currentReadTemperature = sensors.getTempCByIndex(itemCode);
  if (currentReadTemperature != -127) {
    if (WiFi.status() == WL_CONNECTED) {
      if (Firebase.ready()) {
        String _currentDate = getTimeStampNow();
        if (_currentDate != "") {
          currentDate = _currentDate;
          currentDate += currentDate.indexOf("Z") <= 0 ? "Z" : "";
          while (!updateItemHeader(itemCode, currentReadTemperature)) {
            delay(2000);
          }
          Serial.println("[success] update header success!");
          // int timeRead = getCurrentMinute(currentDate.c_str()); // test only
          int timeRead = getCurrentHour(currentDate.c_str());
          if (currentTime != timeRead) {
            while (!insertLog(itemCode, currentReadTemperature, currentDate)) {
              delay(2000);
            }
            Serial.println("[success] logging success!");
            currentTime = timeRead; 
          }

          itemCode = (!qcMode && itemCode < 5) || (qcMode && itemCode < 1) ? itemCode + 1 : 0;
        }
      } else {
        Serial.println("[error] firestore connection not ready");
        resetIfOverfailed();
      }
    } else {
      startWifiConnection();
    }
  } else {
      Serial.printf("[error] DS18B20 sensor [%d] disconnected\n", itemCode);
      resetIfOverfailed();
    }
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
    Serial.println("[error] update header failed!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return false;
  }
}

bool insertLog(int itemCode, float currentReadTemperature, String dateNow) {
  String documentPath = qcMode ? "ITEMSENSORLOGQC" : "ITEMSENSORLOG";
  FirebaseJson content;
  content.set("fields/ItemCode/doubleValue", String(itemCode).c_str());
  content.set("fields/TemperatureValue/doubleValue", String(currentReadTemperature).c_str());
  content.set("fields/CreateDate/timestampValue", String(dateNow).c_str());
  content.set("fields/CreateBy/stringValue", "system");
  if(Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
    // Serial.printf("ok\n%s\n", fbdo.payload().c_str());
    return true;
  } else {
    Serial.println("[error] logging failed!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return false;
  }
}

String getTimeStampNow() {
  unsigned long randTime = millis();
  String serverTimePath = "SERVERTIME/ServerTime";
  FirebaseJson content;
  content.set("fields/CreateBy/stringValue", String(randTime).c_str());
  content.set("fields/LastGet/timestampValue", String(currentDate).c_str());
  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str(), content.raw(), "CreateBy,LastGet")) {
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str())) {
      // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      DynamicJsonDocument doc(500);
      DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());
      if (error) {
        Serial.println("[error] deserializeJson() failed: ");
        Serial.println(error.f_str());
        resetIfOverfailed();
        return "";
      }
      const char* timeResult = doc["updateTime"];
      String date = convertDateTime(timeResult);
      return date;
    } else {
      Serial.println("[error] get server time failled!");
      Serial.println(fbdo.errorReason());
      resetIfOverfailed();
      return "";
    }
  } else {
    Serial.println("[error] set server time failled!");
    Serial.println(fbdo.errorReason());
    resetIfOverfailed();
    return "";
  }
}

void startWifiConnection() {
  #pragma region Reconnect WiFi
  Serial.printf("[info] Connecting ");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[info] Connected with IP: ");
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
      case 5:
        return ITEM_5;
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

int getCurrentHour(const char* date) {
  struct tm tm = {0};
  char buf[100];
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  return tm.tm_hour;
}

int getCurrentMinute(const char* date) {
  struct tm tm = {0};
  char buf[100];
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  return tm.tm_min;
}

void resetIfOverfailed() {
  errorCount++;
  if (errorCount >= 4) {
    Serial.println("[info] resetting device ...");
    resetFunc();
  }
}
