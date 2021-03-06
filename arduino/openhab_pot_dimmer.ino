
/**
 * Simple OpenHAB dimmer based on ESP8266-07 module and potentiometer.
*/
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include "Average.h"

// PIN, which is connected middle of potentiometer
#define POT_PIN  A0
// PWM adjusted pin which is connected to MOSFET gate.
#define PWM_PIN  13

// Local base URL of OpenHAB (can be changed later on config page)
#define DEFAULT_OPENHAB_HOST "192.168.128.100"
#define DEFAULT_OPENHAB_PORT 9000

// Name of OpenHAB item (can be changed later on config page)
#define DEFAULT_OPENHAB_ITEM "kitchenSideboard"

// Update time for item state in seconds;
// dimmer sends periodically its state to OpenHAB
#define ITEM_UPDATE_TIME 60L

// Local network definition; must have static IP address
#define LOCAL_IP   IPAddress(192, 168, 128, 204)
#define GATEWAY    IPAddress(192, 168, 128, 1)
#define SUBNETMASK IPAddress(255, 255, 255, 0)

// Local Wifi AP for connect
#define DEFAULT_WIFI_AP "******"
#define DEFAULT_WIFI_PASSWORD "*******"

// Error count for reconnect WIFI
#define ERR_COUNT_FOR_RECONNECT 30
#define HTTP_READ_TIMEOUT       10000
#define HTTP_CONNECT_TIMEOUT    5000

// Local WEB server port
#define WEB_SERVER_PORT 80

// Magic for detecting empty (unconfigured) EEPROM
#define MAGIC 0x44

// PWM frequuency.
#define PWM_FREQUENCY  120

// When potentiometer change value wait X millis before sending it to OpenHAB.
#define DEFAULT_OH_SEND_TIME 1000L

// POT ADC range
#define DEFAULT_MIN_POT_ADC_VAL 10
#define DEFAULT_MAX_POT_ADC_VAL 1020

// Sensitivity of potentiometer. When value is set from OpenHAB this is minimum change to accept value from potentiometer. In percent.
#define DEFAULT_POT_SENSITIVITY 10

// Nummer of values to average potentiometer. Elimitates jitter and added nice fade efect.
#define POT_AVERAGE_SIZE    500

// Configuration server
ESP8266WebServer *server = (ESP8266WebServer *) NULL;

// Number of communication errors
int errorCount = 0;

// Current light value (0-100) from OpenHAB
int openHabPwmVal = 0;

// Real PWM value is from potentiometer (true) or OpenHAB (false)
bool valFromPot = false;

// Last POT value for determining changes
int lastPotVal = -1;

// Averager for potentiometer vals
Average<int> potAv (POT_AVERAGE_SIZE);


/**
 * EEPROM configuration structure.
*/
struct OhConfiguration {
  int magic;
  char apName [24];
  char password [48];
  char openhabItem [50];
  char openhabHost [50];
  unsigned int openhabPort;
  unsigned int potSensitivity;
  unsigned int potMinAdcVal;
  unsigned int potMaxAdcVal;
  unsigned long openhabSendTime;
};

// Configuration
OhConfiguration config;


/**
 * Inicializes firmware.
*/
void setup() {
  Serial.begin (115200);

  // setup potentiometer pin
  pinMode (POT_PIN, INPUT);

  // setup PWM on output pin
  pinMode (PWM_PIN, OUTPUT);
  analogWriteFreq (PWM_FREQUENCY);
  setPwmVal (0);

  // read config from eeprom
  EEPROM.begin (sizeof (OhConfiguration));
  EEPROM.get (0, config);
  if (config.magic != MAGIC) {
    memset (&config, 0, sizeof (OhConfiguration));
    config.magic = MAGIC;
    updateConfigKey (config.apName, 24, String(DEFAULT_WIFI_AP));
    updateConfigKey (config.password, 48, String(DEFAULT_WIFI_PASSWORD));
    updateConfigKey (config.openhabItem, 50, String(DEFAULT_OPENHAB_ITEM));
    updateConfigKey (config.openhabHost, 50, String(DEFAULT_OPENHAB_HOST));
    config.openhabPort = DEFAULT_OPENHAB_PORT;
    config.potMinAdcVal = DEFAULT_MIN_POT_ADC_VAL;
    config.potMaxAdcVal = DEFAULT_MAX_POT_ADC_VAL;
    config.openhabSendTime = DEFAULT_OH_SEND_TIME;
    config.potSensitivity = DEFAULT_POT_SENSITIVITY;
  }

  // connect to WiFi and create HTTP server
  reconnectWifi();
  createServer();

  delay (1000);

  // Send current light state to OpenHAB; initialy is light off
  updateLightState (openHabPwmVal);
}

/**
 * Helper routine; updates config key.
 *
 * @param c key.
 * @param len max length.
 * @param val value.
*/
void updateConfigKey (char *c, int len, String val) {
  memset (c, 0, len);
  sprintf (c, "%s", val.c_str());
}

/**
 * Reconects WIFI.
*/
void reconnectWifi() {
  Serial.println ("WiFi disconnected...");
  WiFi.disconnect();
  String hostname = String("openhab_") + String(config.openhabItem);
  WiFi.hostname (hostname);
  WiFi.config (LOCAL_IP, GATEWAY, SUBNETMASK);
  WiFi.mode (WIFI_STA);
  delay (1000);

  Serial.print ("Connecting to "); Serial.print (WIFI_AP); Serial.print (' ');
  WiFi.begin (config.apName, config.password);
  delay (1000);
  while (WiFi.status() != WL_CONNECTED) {
    yield();
    delay (500);
    Serial.print (".");
  }
  delay (500);
  Serial.println();
  Serial.println ("WiFi connected...");
}

/**
 * Creates HTTP server.
*/
void createServer() {
  if (server) {
    server->close();
    delete server;
    server = NULL;
  }
  
  // ... and run HTTP server for setup
  server = new ESP8266WebServer (LOCAL_IP, WEB_SERVER_PORT);
  server->on ("/", wsConfig);
  server->on ("/update", wsUpdate);
  server->on ("/reconnect", wsReconnect);
  server->onNotFound (wsUpdatePwmLevel);
  server->begin();

  Serial.println ("Created server...");
}

/**
 * Updates PWM level of light
*/
void wsUpdatePwmLevel() {
  yield();
  
  String url = server->uri();

  Serial.println();
  Serial.print ("GET "); Serial.println (url);

  int pom = url.lastIndexOf ('/');
  if (pom > -1) {
    String val = url.substring (pom + 1);

    for (pom = 0; pom < val.length(); pom++) {
      if (!isDigit (val [pom])) {
        server->send (400, "text/plain", "Bad request");
        return;
      }
    }

    int newVal = (int)constrain (val.toInt(), 0, 100);
    Serial.print ("Update light level to "); Serial.print (newVal); Serial.println ('%');
    
    if (openHabPwmVal != newVal) {
      Serial.println ("Setting value from OpenHAB...");
      openHabPwmVal = newVal;
      valFromPot = false;
    }
    
    server->send (200, "text/plain", "");
  } else {
    server->send (404, "text/plain", "Not found");
  }
}

/**
 * Configuration page.
*/
void wsConfig() {
  yield();

  String resp = "<html><head><title>OpenHAB dimmer configuration</title>";
  resp += "<meta name=\"viewport\" content=\"initial-scale=1.0, width = device-width, user-scalable = no\">";
  resp += "</head><body>";
  resp += "<h1>OpenHAB dimmer configuration</h1>";
  resp += "<form method=\"post\" action=\"/update\" id=\"form\">";
  resp += "<table border=\"0\" cellspacing=\"0\" cellpadding=\"5\">";
  resp += "<tr><td>AP SSID:</td><td><input type=\"text\" name=\"apName\" value=\"" + String(config.apName) + "\" maxlength=\"24\"></td><td></td></tr>";
  resp += "<tr><td>AP Password:</td><td><input type=\"password\" name=\"password\" value=\"" + String(config.password) + "\" maxlength=\"48\"></td><td></td></tr>";
  resp += "<tr><td>OpenHAB item:</td><td><input type=\"text\" name=\"openhabItem\" value=\"" + String(config.openhabItem) + "\" maxlength=\"50\"></td><td></td></tr>";
  resp += "<tr><td>OpenHAB IP address:</td><td><input type=\"text\" name=\"openhabHost\" value=\"" + String(config.openhabHost) + "\" maxlength=\"50\"></td><td></td></tr>";
  resp += "<tr><td>OpenHAB port:</td><td><input type=\"text\" name=\"openhabPort\" value=\"" + String(config.openhabPort) + "\"></td><td></td></tr>";
  resp += "<tr><td>Potentiometer sensitivity [%]:</td><td><input type=\"text\" name=\"potSensitivity\" value=\"" + String(config.potSensitivity) + "\"></td><td></td></tr>";
  resp += "<tr><td>Potentiometer MIN ADC value:</td><td><input type=\"text\" name=\"potMinAdcVal\" value=\"" + String(config.potMinAdcVal) + "\"></td><td></td></tr>";
  resp += "<tr><td>Potentiometer MAX ADC value:</td><td><input type=\"text\" name=\"potMaxAdcVal\" value=\"" + String(config.potMaxAdcVal) + "\"></td><td></td></tr>";
  resp += "<tr><td>Millis before sending to OpenHAB:</td><td><input type=\"text\" name=\"openhabSendTime\" value=\"" + String(config.openhabSendTime) + "\"></td><td></td></tr>";

  resp += "<tr><td colspan=\"3\" align=\"center\"><input type=\"submit\" value=\"Save\"></td></tr>";
  resp += "</table></form>";
  resp += "<p><a href=\"/reconnect\">Reconnect WiFi...</a></p>";
  resp += "</body></html>";

  server->send (200, "text/html", resp);
}

/**
 * Reconnects WiFi with new parameters.
*/
void wsReconnect() {
  yield();
  String resp = "<script>window.alert ('Reconnecting WiFi...'); window.location.replace ('/');</script>";
  server->send (200, "text/html", resp);
  reconnectWifi();
  createServer();
}

/**
 * Saves configuration.
*/
void wsUpdate() {
  yield();

  String apName = server->arg ("apName");
  String password = server->arg ("password");
  String openhabItem = server->arg ("openhabItem");
  String openhabHost = server->arg ("openhabHost");
  unsigned int openhabPort = atoi (server->arg ("openhabPort").c_str());
  unsigned int potMinAdcVal = atoi (server->arg ("potMinAdcVal").c_str());
  unsigned int potMaxAdcVal = atoi (server->arg ("potMaxAdcVal").c_str());
  unsigned long openhabSendTime = atol (server->arg ("openhabSendCycles").c_str());
  unsigned int potSensitivity = atoi (server->arg ("potSensitivity").c_str());
  
  if (apName.length() > 1) {
    updateConfigKey (config.apName, 24, apName);
    updateConfigKey (config.password, 48, password);
    updateConfigKey (config.openhabItem, 50, openhabItem);
    updateConfigKey (config.openhabHost, 50, openhabHost);
    config.openhabPort = openhabPort;
    config.potMinAdcVal = constrain (potMinAdcVal, 0, 1023);
    config.potMaxAdcVal = constrain (potMaxAdcVal, potMinAdcVal, 1023);
    config.openhabSendTime = constrain (openhabSendTime, 100, 60000);
    config.potSensitivity = constrain (potSensitivity, 0, 100);
  
    // store configuration
    EEPROM.begin (sizeof (OhConfiguration));
    EEPROM.put (0, config);
    EEPROM.end();
  
    String resp = "<script>window.alert ('Configuration updated...'); window.location.replace ('/');</script>";
    server->send (200, "text/html", resp);
  } else {
    server->send (200, "text/html", "");
  }
}

/**
 * Calls URL and reads response.
 * 
 * @param url url.
 * @return String response.
*/
String communicate (String url) {
  WiFiClient client;

  // connect to OpenHAB
  Serial.print ("Connecting to "); Serial.print (config.openhabHost); Serial.print (':'); Serial.print (config.openhabPort); Serial.println ("...");
  if (client.connect (config.openhabHost, config.openhabPort)) {
    // send request
    String req = String("GET ") + url + String (" HTTP/1.1\r\n")
      + String("Host: ") + String (config.openhabHost) + String ("\r\nConnection: close\r\n\r\n");
    client.print (req);
    Serial.print (req);

    bool isError = false;
    
    // wait HTTP_CONNECT_TIMEOUT for response
    unsigned long connectStartTime = millis();
    while (client.available() == 0) {
      if (millis() - connectStartTime > HTTP_CONNECT_TIMEOUT) {
        errorCount++;
        isError = true;
        break;
      }

      yield();
    }

    if (!isError) {
      Serial.print ("Reading response -> ");
      
      // read response lines
      unsigned long readStartTime = millis();

      int ch;
      String resp = "";
      while ((ch = client.read()) != -1) {
        resp += (char)ch;
        
        if (millis() - readStartTime > HTTP_READ_TIMEOUT) {
          errorCount++;
          isError = true;
          break;
        }

        yield();
      }

      Serial.println ("OK");
      
      client.stop();
      return resp;
    } else {
      Serial.println ("ERROR");
    }
  }
  client.stop();
  return String("");
}

/**
 * Updates light state on server.
 *
 * @param val value.
*/
void updateLightState (int val) {
  static int lastSendPwmVal = -1;
  yield();
  if (lastSendPwmVal != val) {
    communicate (String("/CMD?") + String(config.openhabItem) + String("=") + String(val));
    lastSendPwmVal = val;
  }
}

/**
 * Sets PWM value
 * 
 * @param val value.
*/
void setPwmVal (int val) {
  // PIN is inverted eq. 1023 is off.
  analogWrite (PWM_PIN, 1023 - map (val, 0, 100, 0, 1023));
}

/**
 * Returns potentiometer value (0-100).
 *
 * @return int value.
*/
int getPotVal() {
  int av = constrain (analogRead (POT_PIN), 0, 1023);
  // This delay is taken from this https://github.com/esp8266/Arduino/issues/1634 discussion. Thank You renno-bih and Internet :)
  delay (3);
  
  av = constrain (1023 - av, config.potMinAdcVal, config.potMaxAdcVal);

  // potentiometer track is divided into 25 level parts
  int resl = map (av, config.potMinAdcVal, config.potMaxAdcVal, 0, 25) * 4;
  if (resl < 0) {
    resl = 0;
  } else if (resl > 100) {
    resl = 100;
  }

  // average value
  potAv.push (resl);
  return potAv.mean();
}

/**
 * Handle requests. 
*/
void loop() {
  static unsigned long lastInteractionTime = 0L;
  static unsigned long lastOpenhabSendTime = millis();

  if (server) {
    server->handleClient();
  }

  int potVal = getPotVal();
  if (lastPotVal < 0) {
    lastPotVal = potVal;
  } else if (abs (potVal - lastPotVal) > config.potSensitivity) {
    Serial.println ("Setting value from potentiometer...");
    valFromPot = true;
  }

  if (valFromPot) {
    lastPotVal = potVal;
  }

  // interaction with OpenHAB once per ITEM_UPDATE_TIME
  if (millis() - lastInteractionTime > ITEM_UPDATE_TIME * 1000L) {
    if (WiFi.status() == WL_CONNECTED) {
      String resp = communicate (String("/rest/items/") + String(config.openhabItem) + String("/state"));

      // extract text response
      int lip = resp.lastIndexOf ('\n');
      resp = resp.substring (lip + 1);

      if (resp.startsWith (String("Un"))) {
        Serial.println ("Unknown value:");
        Serial.println (resp);
        Serial.println ("> updating state...");
        updateLightState (openHabPwmVal);
        errorCount = 0;
      } else {
        int level = resp.toInt();
        Serial.print ("Updating level to: "); Serial.print (level); Serial.println ('%');
        if (level != openHabPwmVal) {
          Serial.println ("Setting value from OpenHAB...");
          valFromPot = false;
          openHabPwmVal = level;
        }
        errorCount = 0;
      }
    } else {
      Serial.println ("Wifi not connected!");
      errorCount++;
    }
      
    // when error count reaches ERR_COUNT_FOR_RECONNECT, reconnect WiFi.
    if (errorCount > ERR_COUNT_FOR_RECONNECT) {
      errorCount = 0;
      reconnectWifi();
      createServer();
    }

    lastInteractionTime = millis();
  }

  // Update PWM by the potentiometer or OpenHAB value
  if (valFromPot) {
    setPwmVal (potVal);

    // Send potentiometer change once per openHabSendTime
    if (millis() - lastOpenhabSendTime > config.openhabSendTime) {
      openHabPwmVal = potVal;
      lastOpenhabSendTime = millis();
      updateLightState (potVal);
    }
  } else {
    setPwmVal (openHabPwmVal);
  }

  yield();
}

