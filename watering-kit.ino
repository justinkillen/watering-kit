/**************************************************
 * This code has been tested with the Elecrow
 * watering kit that has an integrated 
 * Arduino Leonardo.
 * 
 * Make sure to set your Board and Port 
 * appropriatly. See the README.md for programming
 * notes.
 **************************************************/

#include <Wire.h>
#include <U8g2lib.h>
#include <U8x8lib.h>
#include <RTClib.h>
#include <VL53L0X.h>

// Review all of the values found here.
// (See watering-kit-config.h-sample)
#include "watering-kit-config.h"

U8G2_SH1106_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, /* clock=*/SCL, /* data=*/SDA, /* reset=*/U8X8_PIN_NONE);
RTC_DS1307 RTC;

// Time (millis()) we last sent stats
unsigned long send_stats_last = 0;

// If we want to force sending stats on this iteration
bool send_stats_force = false;

// The number of sensors. If you want more, you will need
// to many of the below arrays, too.
int num_sensors = 4;

// set water pump
int pump_pin = 4;

// set button
int button_pin = 12;

// set water valve pins
int valve_pins[] = {6, 8, 9, 10};

// set all moisture sensors PIN ID
int moisture_pins[] = {A0, A1, A2, A3};

// declare moisture values
int moisture_values[] = {0, 0, 0, 0};

//valve states    1:open   0:close
int valve_state_flags[] = {0, 0, 0, 0};

//pump state    1:open   0:close
int pump_state_flag = 0;

// Water level
bool water_level_enabled = false;
VL53L0X water_level_sensor;
uint16_t water_level_per = 0;
uint16_t water_level_mm = 0;

// Values to help improve the capacitive sensor accuracy.
long mostDrySensorValue[] = DRY_VALUES
long mostWetSensorValue[] = WET_VALUES

static char output_buffer[10];

void setup()
{
  Wire.begin();
  RTC.begin();

  Serial.begin(19200);
  
#ifdef SEND_STATS_MQTT
  // Serial to ESP8266. Use RX & TX pins of Elecrow watering board
  Serial1.begin(19200);
  Serial.println("Started Serial1 for esp8266");
#endif

  u8g2.begin();
  // declare valve relays as output
  for (int i = 0; i < num_sensors; i++)
  {
    pinMode(valve_pins[i], OUTPUT);
  }
  // declare pump as output
  pinMode(pump_pin, OUTPUT);
  // declare switch as input
  pinMode(button_pin, INPUT);

  setup_water_level_sensor();
}

void setup_water_level_sensor()
{
  water_level_enabled = false;
  water_level_sensor.setTimeout(500);
  if (water_level_sensor.init())
  {
    water_level_enabled = true;
    // High accuracy - increase timing budget to 200 ms
    water_level_sensor.setMeasurementTimingBudget(200000);
  }
}

void loop()
{
  // read the value from the moisture sensors:
  read_value();
  check_water_level();
  water_flower();
  send_stats();
  u8g2.firstPage();
  do
  {
    draw_stats();
  } while (u8g2.nextPage());
  delay(1000);
}

//Set moisture value
void read_value()
{
  /**************These is for resistor moisture sensor***********
  float value = analogRead(A0);
  moisture_values[i] = (value * 120) / 1023; delay(20);
  ...
 **********************************************************/
  /************These is for capacity moisture sensor*********/
  for (int i = 0; i < num_sensors; i++)
  {
    float value = analogRead(moisture_pins[i]);

    if (value > mostDrySensorValue[i]) {
      // Tune mostDrySensorValue[i] max value
      mostDrySensorValue[i] = value;
    }
    if (value < mostWetSensorValue[i]) {
      // Tune mostWetSensorValue[i] max value
      mostWetSensorValue[i] = value;
    }    

    // Conver mosisture readings to 0-100 percentage.
    moisture_values[i] = map(value, 
        mostDrySensorValue[i], mostWetSensorValue[i], 0, 100);
    if (moisture_values[i] < 0)
    {
      moisture_values[i] = 0;
    }
    delay(20);
  }
}

void water_flower()
{
  for (int i = 0; i < num_sensors; i++)
  {
    if (moisture_values[i] < WATER_START_VALUE)
    {
      digitalWrite(valve_pins[i], HIGH);
      if (valve_state_flags[i] != 1) 
      {
        valve_state_flags[i] = 1;
        send_stats_force = true;
      }
      delay(50);
      if (pump_state_flag == 0)
      {
        digitalWrite(pump_pin, HIGH);
        pump_state_flag = 1;
        delay(50);
      }
    }
    else if (moisture_values[i] > WATER_STOP_VALUE)
    {
      if (valve_state_flags[i] != 0) 
      {
        // Only report if it IS on
        send_stats_force = true;
        valve_state_flags[i] = 0;
      }
      // Force it off
      digitalWrite(valve_pins[i], LOW);
      delay(50);
    }
  }

  // If no more active valves, shut down the pump.
  int num_active_valves = 0;
  for (int i = 0; i < num_sensors; i++)
  {
    num_active_valves += (valve_state_flags[i] > 0) ? 1 : 0;
  }
  if (num_active_valves == 0)
  {
    digitalWrite(pump_pin, LOW);
    pump_state_flag = 0;
    delay(50);
  }
}

void check_water_level()
{
  if (water_level_enabled)
  {
    // This will return WATER_LEVEL_TIMEOUT if a timeout happens.
    water_level_mm = water_level_sensor.readRangeSingleMillimeters();
    if (water_level_mm == WATER_LEVEL_TIMEOUT)
    {
      //Timeout reading the water level
      water_level_per = WATER_LEVEL_TIMEOUT;
    }
    else
    {
      if (water_level_mm > MAX_WATER_DEPTH)
      {
        water_level_mm = MAX_WATER_DEPTH;
      }
      water_level_mm = MAX_WATER_DEPTH - water_level_mm;
      water_level_per = (uint16_t) (((double) water_level_mm / (double) MAX_WATER_DEPTH) * 100);
    }
  }
}

void send_stats_serial(Stream &port)
{  
  for (int i = 0; i < num_sensors; i++)
  {
    /*********Output Moisture Sensor values to ESP8266******/
    dtostrf(moisture_values[i], 4, 0, output_buffer);
    if (i != 0) {
      port.print(",");
    }
    port.print(output_buffer);
  }

  dtostrf(pump_state_flag, 1, 0, output_buffer);
  port.print(",");
  port.print(output_buffer);

  dtostrf(water_level_mm, 4, 0, output_buffer);
  port.print(",");
  port.print(output_buffer);

  dtostrf(water_level_per, 4, 0, output_buffer);
  port.print(",");
  port.print(output_buffer);

  for (int i = 0; i < num_sensors; i++)
  {
    /*********Output Moisture Sensor values to ESP8266******/
    dtostrf(valve_state_flags[i], 1, 0, output_buffer);
    port.print(",");
    port.print(output_buffer);
  }

  // End the message.
  port.print("\n");
}

void send_stats() {
  unsigned long now = millis();
  unsigned long millisSinceLastRun = now - send_stats_last;
  
#ifdef SEND_STATS_MQTT
  dtostrf(millisSinceLastRun, 9, 0, output_buffer);
  Serial1.print("#Millis since last run ");
  Serial1.print(millisSinceLastRun);
  Serial1.print("\n");
#endif

  if (millisSinceLastRun > SEND_STATS_FREQ_MS) {
    // TODO: There is some issue that this isn't ever being triggered.
    send_stats_force = true;
  }
  if (send_stats_force)
  {
    send_stats_last = now;
    send_stats_force = false;
#ifdef SEND_STATS_LOCAL
    send_stats_serial(Serial);
#endif
#ifdef SEND_STATS_MQTT
    send_stats_serial(Serial1);
#endif
  }
}

void draw_stats()
{
  int x_offsets[] = {0, 32, 64, 96};
  char display_buffer[5] = {0};

  u8g2.setFont(u8g2_font_8x13_tr);
  u8g2.setCursor(9, 60);
  u8g2.print("W. LEVEL");
  if (!water_level_enabled)
  {
    u8g2.drawStr(x_offsets[2] + 16, 60, "N/C");
  }
  else if (water_level_per == WATER_LEVEL_TIMEOUT)
  {
    u8g2.drawStr(x_offsets[2] + 16, 60, "T/O");
  }
  else
  {
    itoa(water_level_per, display_buffer, 10);
    // itoa(water_level_mm, display_buffer, 10);
    u8g2.drawStr(x_offsets[2] + 16, 60, display_buffer);
  }

  for (int i = 0; i < num_sensors; i++)
  {
    itoa(moisture_values[i], display_buffer, 10);
    if (moisture_values[i] < 10)
    {
      u8g2.drawStr(x_offsets[i] + 14, 45, display_buffer);
    }
    else if (moisture_values[i] < 100)
    {
      u8g2.drawStr(x_offsets[i] + 5, 45, display_buffer);
    }
    else
    {
      moisture_values[i] = 100;
      itoa(moisture_values[i], display_buffer, 10);
      u8g2.drawStr(x_offsets[i] + 2, 45, display_buffer);
    }
    u8g2.setCursor(x_offsets[i] + 23, 45);
    u8g2.print("%");
  }
}
