/*
Smart garage door opener

Upgrade your conventional garage door into a smart one!
This project seamlessly integrates your garage door opener with an external switch contact (e.g., Novoport)
into your smart home environment using MQTT, specifically tailored for use with Home Assistant.
*/

#include "EspMQTTClient.h"
#include <U8g2lib.h>
#include <VL53L1X.h>
#include <Wire.h>

// Relay pin
const int relay = 13; // D7

// Distance sensor
VL53L1X sensor;
int distance_mm = 0;

// Submission intervals
long interval_sensor_readings = 1000;             // ms
const long interval_publish_end_positions = 5000; // ms
const long interval_publish_moving = 1000;        // ms

float distance_mm_open_positions = 240;    // mm
float distance_mm_closed_positions = 2540; // mm

unsigned long previous_millis_sensor = 0;
unsigned long previous_millis_publish = 0;

// Interval of publishing sensor readings
unsigned long submitInterval = 1000; // Start with 1 second interval

// Garage MQTT Topic
#define MQTT_PUB_DISTANCE_MM "garage_door/VL53L1X/distance_mm"

// MQTT subsciption topics
#define MQTT_SUB_RELAY "garage_door/relay"

// MQTT message buffer
#define MQTT_SUB_RELAY_COMMAND "pulse"

// U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);   // All Boards without Reset of the Display
#define SCL 12 /*D5=SLC=GPIO12*/
#define SDA 14 /*D6=SDA=GPIO14*/
U8G2_SSD1306_128X64_NONAME_F_SW_I2C
u8g2(U8G2_R0, /* clock=*/SCL, /* data=*/SDA, /* reset=*/U8X8_PIN_NONE); // All Boards without Reset of the Display

// EspMQTTClient client
EspMQTTClient client(
    "SSID",          // Wifi SSID
    "PASSWORD",      // Wifi Password
    "192.168.178.X", // MQTT Broker server ip
    "MQTT_USER",     // MQTT User - Can be omitted if not needed
    "MQTT_PASSWORD", // MQTT User -Can be omitted if not needed
    "CLIENT_NAME",   // Client name that uniquely identify your device
    1883             // The MQTT port, default to 1883. this line can be omitted
);

void setup()
{
  Serial.begin(115200);
  Serial.println("Setup start");
  u8g2.begin(); // connect to the display via I2C. You can use any address that is available

  // Initialize the relay pin as an output and set to high
  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH);
  Wire.begin();
  Wire.setClock(400000); // use 400 kHz I2C

  // Initialize the distance sensor
  sensor.setTimeout(500);
  if (!sensor.init())
  {
    Serial.println("Failed to detect and initialize sensor!");
    while (1)
      ;
  }
  sensor.setDistanceMode(VL53L1X::Long);
  sensor.setMeasurementTimingBudget(50000);
  sensor.startContinuous(50);
  Serial.println("Sensor online!");

  // Optional functionalities of EspMQTTClient
  client.enableDebuggingMessages();                                           // Enable debugging messages sent to serial output
  client.enableHTTPWebUpdater();                                              // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overridded with enableHTTPWebUpdater("user", "password").
  client.enableOTA();                                                         // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  client.enableMQTTPersistence();                                             // Enable MQTT persistence. Save the last MQTT connection, and restores it on boot up.
  client.setMqttReconnectionAttemptDelay(1000);                               // Set the delay between reconnection attemps. Default is 5000ms.
  client.enableLastWillMessage("garage_door/lastwill", "I am going offline"); // You can activate the retain flag by setting the third parameter to true
  Serial.println("Setup done");
}

// This function is called once everything is connected (Wifi and MQTT)
// WARNING : YOU MUST IMPLEMENT IT IF YOU USE EspMQTTClient
void onConnectionEstablished()
{
  Serial.println("I'm connected");

  // Subscribe to MQTT topic and display received message to Serial
  client.subscribe(MQTT_SUB_RELAY, [](const String &payload)
                   {    
            if (strcmp(payload.c_str(), MQTT_SUB_RELAY_COMMAND) == 0)
    {
    Serial.println(payload);
    u8g2.clearBuffer();                  // clear the internal memory
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, MQTT_SUB_RELAY); // write something to the internal memory
    u8g2.drawStr(0, 25, payload.c_str());         // write something to the internal memory
    u8g2.sendBuffer();                   // transfer internal memory to the display

    Serial.println("Open_door");
    digitalWrite(relay, LOW);
    Serial.println("Relay: ON");
    unsigned long startTime = millis();
    while (millis() - startTime < 300)
    {
      // Do nothing, just wait
    }
    digitalWrite(relay, HIGH);
    Serial.println("Relay: OFF"); 
    } });

  // Publish a message to "mytopic/test"
  client.publish("garage_door/debug", "This is a message");
}

void loop()
{
  client.loop();


  // Sensor readings
  if (millis() - previous_millis_sensor >= interval_sensor_readings)
  {
    previous_millis_sensor = millis();

    sensor.read();
    distance_mm = sensor.ranging_data.range_mm;

    char buffer[20];                           // Buffer for the text
    sprintf(buffer, "%i", distance_mm);        // Use sprintf to format the text
    u8g2.clearBuffer();                        // clear the internal memory
    u8g2.setFont(u8g2_font_ncenB08_tr);        // choose a suitable font
    u8g2.drawStr(0, 10, MQTT_PUB_DISTANCE_MM); // write something to the internal memory
    u8g2.drawStr(0, 25, buffer);               // write something to the internal memory
    u8g2.drawStr(0, 40, ("Wifi: " + String(client.isWifiConnected())).c_str());
    u8g2.drawStr(0, 55, ("MQTT: " + String(client.isMqttConnected())).c_str());
    u8g2.sendBuffer(); // transfer internal memory to the display
    Serial.printf("Distance: %i mm\n", distance_mm);

    // Update the submit interval based on the mean distance
    if (distance_mm < distance_mm_open_positions || distance_mm > distance_mm_closed_positions)
    {
      submitInterval = interval_publish_end_positions;
    }
    else
    {
      submitInterval = interval_publish_moving;
    }
  }

  if (millis() - previous_millis_publish >= submitInterval)
  {
    previous_millis_publish = millis();

    sensor.read();
    distance_mm = sensor.ranging_data.range_mm;

    client.publish(MQTT_PUB_DISTANCE_MM, String(distance_mm).c_str());
    Serial.printf("Message: %i mm\n", distance_mm);
  }
}