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
const int num_sensors = 1;

// overflow sensor - it assumes to be num_sensors + 1
bool overflow_enabled = true;

bool force_pump_shutdown = false;

// set water pump
const int pump_pin = 4;

// set button
const int button_pin = 12;

// set water valve pins
const int valve_pins[] = {6, 8, 9, 10};

// set all moisture sensors PIN ID
const int moisture_pins[] = {A0, A1, A2, A3};

// declare moisture values
int moisture_values[] = {0, 0, 0, 0};

enum PUMP_VALVE_STATE {CLOSED = 0, OPEN = 1};

// valve states    1:open   0:closed
PUMP_VALVE_STATE valve_state_flags[] = {CLOSED, CLOSED, CLOSED, CLOSED};

// pump state    1:open   0:closed
PUMP_VALVE_STATE pump_state_flag = CLOSED;

// moisture metrics ring-buffer.  128 slots at 15 minutes each gives us 32 hours of data.
const int MOISTURE_RINGBUFFER_SLOTS = 128;
const int MOISTURE_RINGBUFFER_INTERVAL_SECONDS = 15 * 60;
int moisture_ringbuff[MOISTURE_RINGBUFFER_SLOTS] = {};
int moisture_ringbuff_current = 0;
DateTime moisture_ringbuff_advance;

// cooldown
DateTime cooldown_until[] = {DateTime((uint32_t)0), DateTime((uint32_t)0), DateTime((uint32_t)0), DateTime((uint32_t)0)};
DateTime water_cutoff_at[] = {DateTime((uint32_t)0), DateTime((uint32_t)0), DateTime((uint32_t)0), DateTime((uint32_t)0)};
bool run_water[] = {false, false, false, false};

// Water level
bool water_level_enabled = false;
VL53L0X water_level_sensor;
uint16_t water_level_per = 0;
uint16_t water_level_mm = 0;

// Values to help improve the capacitive sensor accuracy.
long mostDrySensorValue[] = DRY_VALUES
long mostWetSensorValue[] = WET_VALUES

static char output_buffer[10];

const int COL_WIDTH = 32;
const int x_offsets[] = {COL_WIDTH*0, COL_WIDTH*1, COL_WIDTH*2, COL_WIDTH*3};

void setup()
{
  Wire.begin();
  RTC.begin();
  Serial.begin(19200);
  for(int x=0; x < MOISTURE_RINGBUFFER_SLOTS; x++)
  {
    moisture_ringbuff[x] = -1;
  }
  debugf("setup()\n");

  // // fill ringbuffer
  // for (int x=0; x < MOISTURE_RINGBUFFER_SLOTS; x++)
  // {
  //   moisture_ringbuff[x] = x;
  // }
  
#ifdef SEND_STATS_MQTT
  // Serial to ESP8266. Use RX & TX pins of Elecrow watering board
  Serial1.begin(19200);
  Serial.println("Started Serial1 for esp8266");
#endif

  u8g2.begin();
  DateTime now = RTC.now();
  // declare valve relays as output
  for (int i = 0; i < num_sensors; i++)
  {
    pinMode(valve_pins[i], OUTPUT);
    // init cooldown timers
    // and a bonus: delay first pump, giving sensors a warm-up period
    cooldown_until[i] = now + TimeSpan((int32_t)COOLDOWN_PERIOD_SECONDS);
    water_cutoff_at[i] = now;
  }
  // declare pump as output
  pinMode(pump_pin, OUTPUT);
  // declare switch as input
  pinMode(button_pin, INPUT);

  setup_water_level_sensor();
}

void next_ringbuffer()
{
  DateTime now = RTC.now();
  if (now > moisture_ringbuff_advance)
  {
    debugf("rolling over to next ringbuffer\n");
    moisture_ringbuff_advance = RTC.now() + TimeSpan(MOISTURE_RINGBUFFER_INTERVAL_SECONDS);
    moisture_ringbuff_current++;
    if (moisture_ringbuff_current >= MOISTURE_RINGBUFFER_SLOTS) {
      moisture_ringbuff_current = 0;
    }
  }
  else 
  {
    moisture_ringbuff[moisture_ringbuff_current] = moisture_values[0];
  }
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
  read_value();
  check_water_level();
  water_flower();
  next_ringbuffer();
  send_stats();
  u8g2.firstPage();
  do
  {
    draw_stats();
    draw_graph();
  } while (u8g2.nextPage());
  delay(200);
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

  int loop_count = num_sensors;
  if(overflow_enabled)
  {
    loop_count++;
  }

  for (int i = 0; i < loop_count; i++)
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
    // min/max macros are weird - write to tmp variables
    int tmp = map(value, mostDrySensorValue[i], mostWetSensorValue[i], 0, 100);
    int tmp2 = min(100, tmp);
    moisture_values[i] = max(0, tmp2);
    delay(20);
  }
}

void water_flower()
{
  force_pump_shutdown = false;
  if(water_level_enabled)
  {
    // reservoir is empty?
    force_pump_shutdown |= water_level_mm < 0;
  }
  if(overflow_enabled)
  {
    // overflow?
    force_pump_shutdown |= moisture_values[num_sensors] > OVERFLOW_TRIGGER_THRESHOLD;
  }

  // assume all valves should be closed, and that we should not run the pump
  PUMP_VALVE_STATE desired_state[] = {CLOSED, CLOSED, CLOSED, CLOSED};
  PUMP_VALVE_STATE desired_pump_state = CLOSED;

  DateTime now = RTC.now();
  for (int i = 0; i < num_sensors; i++)
  {
    debugf(" currently: %s", valve_state_flags[i] == OPEN ? "OPEN" : "CLOSED");
    debugf(" cooldown: %" PRIu32, cooldown_until[i].unixtime());
    debugf(" water_cutoff_at: %" PRIu32, water_cutoff_at[i].unixtime());
    debugf(" moisture_values: %d", moisture_values[i]);
    debugf(" now: %" PRIu32, now.unixtime());
    debugf("\n");

    if (force_pump_shutdown) {
      debugf("Forced shutdown mode\n");
      continue;
    }

    if (valve_state_flags[i] == CLOSED)
    {
      // see if we should turn on
      if (moisture_values[i] < WATER_START_VALUE)
      {
        run_water[i] = true;
      }

      if (run_water[i] && cooldown_until[i] <= now)
      {
        debugf("Setting OPEN %d\n", i);
        desired_state[i] = OPEN;
        desired_pump_state = OPEN;
      }
    }
    else
    {
      // see if we should stay on
      if (moisture_values[i] > WATER_STOP_VALUE)
      {
        run_water[i] = false;
      }
      if (run_water[i] && water_cutoff_at[i] > now)
      {
        debugf("Leaving OPEN %d\n", i);
        desired_state[i] = OPEN;
        desired_pump_state = OPEN;
      }
    }
  }

  // always stop the pump before closing valves
  if(pump_state_flag == OPEN && desired_pump_state == CLOSED)
  {
    debugf("Stop pump\n");
    digitalWrite(pump_pin, LOW);
    pump_state_flag = CLOSED;
    delay(50);
  }

  bool valves_changed = false;
  for (int i = 0; i < num_sensors; i++)
  {
    if (valve_state_flags[i] != desired_state[i])
    {
      debugf("Changing valve %d to %s\n", i, desired_state[i] == OPEN ? "OPEN" : "CLOSED");

      valves_changed = true;
      if(desired_state[i] == OPEN)
      {        
        water_cutoff_at[i] = now + TimeSpan((int32_t)MAX_WATERING_LENGTH_SECONDS);
        digitalWrite(valve_pins[i], HIGH);
      }
      else if (desired_state[i] == CLOSED)
      {
        cooldown_until[i] = now + TimeSpan((int32_t)COOLDOWN_PERIOD_SECONDS);
        digitalWrite(valve_pins[i], LOW);
      }

      valve_state_flags[i] = desired_state[i];
      send_stats_force = true;
    }
  }

  if (pump_state_flag == CLOSED && desired_pump_state == OPEN)
  {
    if(valves_changed)
    {
      delay(50);  // give the valves time to change before engaging the pump
    }
    debugf("Start pump\n");
    digitalWrite(pump_pin, HIGH);
    pump_state_flag = OPEN;
  }

  debugf("\n");
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

  if (millisSinceLastRun < SEND_STATS_FREQ_MS) {
    // not ready yet
    return;
  }

  send_stats_last = now;
  send_stats_force = false;
#ifdef SEND_STATS_LOCAL
  send_stats_serial(Serial);
#endif
#ifdef SEND_STATS_MQTT
  send_stats_serial(Serial1);
#endif
}

void draw_stats()
{
  DateTime now = RTC.now();
  const int DISPLAY_BUFFER_SIZE = 5;
  char display_buffer[DISPLAY_BUFFER_SIZE] = {0};

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
    // moisture level, right-justified
    sprintf(display_buffer, "%d%%", moisture_values[i]);
    u8g2DrawStrRightJustifiedClearPrefix(x_offsets[0], x_offsets[0] + COL_WIDTH, 45 - 15 * i, display_buffer);


    // hold state
    DateTime until;
    if(valve_state_flags[i] == OPEN)
    {
      until = water_cutoff_at[i];
    } else {
      until = cooldown_until[i];
    }

    TimeSpan secs_remaining = until - now;
    char run_water_indicator = '-';
    if (run_water[i])
    {
      sprintf(display_buffer, "%" PRId32, secs_remaining.totalseconds());
      run_water_indicator = valve_state_flags[i] == OPEN ? 'W' : 'H';
    }
    else
    {
      // clear countdown area
      display_buffer[0] = '\0';
    }
    u8g2DrawStrRightJustifiedClearPrefix(x_offsets[2], x_offsets[2] + COL_WIDTH, 45 - 15 * i, display_buffer);

    sprintf(display_buffer, "%c%c", run_water_indicator, force_pump_shutdown ? 'V' : '-');
    u8g2DrawStrRightJustifiedClearPrefix(x_offsets[3], x_offsets[3] + COL_WIDTH, 45 - 15 * i, display_buffer);
  }
}

void draw_graph()
{
  // might need to break the calculations out of the draw loop if it takes a long time

  // we have the top half of the screen, roughly 0,0 to 127,31
  // first, we separate into 3 vertical areas:
  // start, height, notes
  //  0,  5,  4, moisture above WATER_STOP_VALUE
  //  5,  1,  5, line separator
  //  6, 20, 25, moisture between WATER_START_VALUE and WATER_STOP_VALUE
  // 26,  1, 26, line separator
  // 27,  5, 31, moisture below WATER_START_VALUE
  // total: 32px

  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 0, 127, 31);
  u8g2.setDrawColor(1);

  int ringbuff_offset = moisture_ringbuff_current;
  for (int x = 127; x >= 0; x--)
  {
    if (x % 2 == 0)
    {
      u8g2.drawPixel(x, 5);
      u8g2.drawPixel(x, 26);
    }

    if (moisture_ringbuff[ringbuff_offset] != -1)
      int bit = calc_graph(moisture_ringbuff[ringbuff_offset]);
      u8g2.drawPixel(x, bit);
    }

    ringbuff_offset--;
    if (ringbuff_offset < 0)
    {
      ringbuff_offset = MOISTURE_RINGBUFFER_SLOTS - 1;
    }
  }
}

int calc_graph(int moisture_level)
{
  int display_top = 0;
  int display_height = 0;
  int percent_range = 0;
  int percent_in_range = 0;

  if (moisture_level > WATER_STOP_VALUE)
  {
    display_top = 0;
    display_height = 5;
    percent_range = 100 - WATER_STOP_VALUE;
    percent_in_range = moisture_level - 1 - WATER_STOP_VALUE;
  }
  else if (moisture_level >= WATER_START_VALUE && moisture_level <= WATER_STOP_VALUE)
  {
    display_top = 6;
    display_height = 20;
    percent_range = WATER_STOP_VALUE - WATER_START_VALUE - 1;
    percent_in_range = moisture_level - 1 - WATER_START_VALUE;
  }
  else if (moisture_level < WATER_START_VALUE)
  {
    display_top = 27;
    display_height = 5;
    percent_range = WATER_START_VALUE;
    percent_in_range = moisture_level;
  }

  int display_bottom = display_top + display_height - 1;
  return display_bottom - (int)(1.0 * percent_in_range * display_height / percent_range);
}

void u8g2DrawStrRightJustifiedClearPrefix(u8g2_uint_t left, u8g2_uint_t right, u8g2_uint_t bottom, const char *s)
{
    int width = u8g2.getStrWidth(s);
    int height = 15; //u8g2.getMaxCharHeight();
    int oldDrawColor = u8g2.getDrawColor();

    // draw the blank area before the string
    u8g2.setDrawColor(0);
    // drawStr y is bottom edge, but drawBox y is top edge
    u8g2.drawBox(left, bottom - height, width, height);

    // now draw the 
    u8g2.setDrawColor(1);
    u8g2.drawStr(right - width, bottom, s);

    u8g2.setDrawColor(oldDrawColor);
}

void debugf(char* format, ...)
{
  va_list argptr;
  char buffer[129] = { 0 };

  va_start(argptr,format);
  #ifndef SEND_STATS_LOCAL
  vsnprintf(buffer, 128, format, argptr);
  #endif
  va_end(argptr);
  #ifndef SEND_STATS_LOCAL
  Serial.print(buffer);
  #endif
}
