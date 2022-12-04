/*
 * BD / 20221126
 * wekker02.ino
 *
 * Wekker project
 *
 * Use with Adafruit ESP32 Feather hardware 
 *
 * A sketch to control an alarm clock (dutch: wekker)
 *
 * See project directory for the designed FSM and pseudo code.
 *
 * General structure:
 *
 * Contrary to the previous version of this code, this code does not make use of deep sleep. So it assumes the 
 * alarm clock will be connected to mains power permanently. And there will be no need for the use of deep sleep.
 * After power up the alarm clock may be controlled by touch pads, software timers 
 * and remotely from an external application.
 * The main control flow handles events to execute an FSM.
 * Major states are:
 * - the UNKNOWN state
 *   The state on power-up.
 * - the STARTED state. 
 *   The alarm clock is activated and the alarm clock currently shows a non-obtrusive 'armed' indication but does not sound anything.
 * - the STOPPED state. 
 *   The alarm clock is stopped and does not show or sound anything.
 * - the ALARMING state.
 *   The alarm clock is alarming and shows the clock display and sounds the radio. This state exists for some time
 *   until it expires or until stopped or snoozed.
 * - the SNOOZED state
 *   The snooze clock is activated and the alarm clock currently shows a non-obtrusive snoozed indication but does not sound anything. 
 *   This state exists for some time until it expires or until the proper pads are touched.
 * - the ATTENTIVE state
 *   The alarm and snooze clock are deactivated, The clock may show, the radio may sound, an attention indicator is shown. Several indicators are active.
 *   The alarm clock can be remotely managed. Operational parameters (eg. the alarm time) may be adapted.
 *
 * An event may be the touch of a touchpad, the expiration of a timer or possibly a message from a dedicated remote control application..
*/


//=============================================================================================== 
//
// System specific includes
//
#include <stdlib.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <WiFi.h>
#include "time.h"
#include "sntp.h"
#define BENZASYNCUDP 1
#ifdef BENZASYNCUDP
#include "AsyncUDP.h"
#endif

boolean benz_verbose    = true; // to generate verbose printed messages

const char* ssid       = "BenzDlinkDraadloosNet2";
const char* password   = "XXXXXX";

const char* ntpServer1 = "nl.pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

//const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)


     
//=============================================================================================== 
//
// System specific defines
//
// Used pins ...
//
// Touch pins (used defines in esp.h)
// 12/T5/IO12/ADC2_CH5/A11   -- left touch pad
// 27/T7/IO27/ADC2_CH7/A10   -- middle touch pad
// 33/T8/IO33/ADC1_CH5/A9    -- right touch pad
//
// Which pin on the system is connected to the Neopixels?
#define PIXELPIN  15 // On Wekker PCB with Adafruit ESP32 Feather
//
// How many Neog_pixels are attached to the system?
#define NUMPIXELS 43          // Number of all g_pixels daisychained in the wekker
// The outer minutes ring ....
#define MINUTES_RING_IDX  0   // the index of the first pixel on the outer minute ring
#define MINUTES_RING_NRPX 24  // the number of g_pixels on the minute ring
// so ... 2,5 minute per pixel
//
// The next middle hours ring ....
// the index of the first pixel on the middle hours ring
#define HOURS_RING_IDX  MINUTES_RING_NRPX    
#define HOURS_RING_NRPX 12    // the number of g_pixels on the hours ring
// so ... 1 per pixel modulo 12
//                             
// The next inner 10-seconds ring  
// the index of the first pixel on the 10-seconds ring
// N.B. comes after the inner most single pixel
#define TENSECONDS_RING_IDX  (MINUTES_RING_NRPX + HOURS_RING_NRPX + 1)   
#define TENSECONDS_RING_NRPX 6   // the number of g_pixels on the 10-seconds ring
// so ... 10 seconds per pixel
//
// the solitary inner most single pixel
#define CENTREPIXELIDX (MINUTES_RING_NRPX + HOURS_RING_NRPX)

//
//
// The radio is controle by I2C and its default SDA/SCL pins

//
// The touchpad sensitivities
#define T5_TOUCH_SENSITIVITY 49 //   left - startstop
#define T7_TOUCH_SENSITIVITY 49 // middle - snooze
#define T8_TOUCH_SENSITIVITY 57 //  right - attention

//=============================================================================================== 
//
// Project specific includes
//

//=============================================================================================== 
//
// Project specific defines
//
// Application states
//
#define WKST_UNKNOWN      0
#define WKST_WAKEING      1 // for historic reasons
#define WKST_STARTED      2
#define WKST_STOPPED      3
#define WKST_SNOOZED      4
#define WKST_ALARMING     5
#define WKST_ATTENTIVE    6
//
// Application events handled 
//
#define WKEV_UNKNOWN                  0
#define WKEV_TIMER_EXPIRED            1
#define WKEV_STARTSTOP_TOUCHED        2
#define WKEV_SNOOZE_TOUCHED           3
#define WKEV_ATTENTION_TOUCHED        4
#define WKEV_REMOTECONTROLMSG         5
#define WKEV_SPURIOUS                 6
#define WKEV_NONE                     7
#define WKEV_POWERUP                  8
//
// ....
//

//=============================================================================================== 
//
// System global variables
//
// The neog_pixels
//
// When setting up the NeoPixel library, we tell it how many g_pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel g_pixels(NUMPIXELS, PIXELPIN, NEO_GRB + NEO_KHZ800);

#ifdef BENZASYNCUDP
// An UDP protocol object
AsyncUDP Udp;
#endif // BENZASYNCUDP

//=============================================================================================== 
//
// Project global variables
//
// Application state
unsigned short g_current_app_state      = WKST_UNKNOWN;
unsigned short g_previous_app_state     = WKST_UNKNOWN;
unsigned long  g_time_elapsed_in_state  = 0; // in 100ms units. Eg. 1 second elapsed in state equals 10.

// Current application event
unsigned short g_cur_app_event      = WKEV_POWERUP;
unsigned short g_rc_event           = WKEV_NONE;

// Remote controlled overrides
boolean g_show_clock_anyway         = false;  // set/reset by remote command
boolean g_show_seconds_anyway       = false;  // set/reset by remote command

// -- related to the operation of the alarm clock
// -- should become member of a class of its own
unsigned short g_cur_time_h         = 21; // 0..23
unsigned short g_cur_time_m         = 0; // 0..59
unsigned short g_cur_time_s         = 0; // 0..59
unsigned short g_alarm_time_h       = 7; // 0..23
unsigned short g_alarm_time_m       = 0; // 0..59
unsigned short g_alarming_duration  = 3000;
unsigned short g_snooze_duration    = 3000;
unsigned short g_attentive_duration  = 600;

// -- related to the operation of the radio
// -- should become member of a class of its own
boolean       g_radio_state         = false;   
unsigned char g_frequencyH          =  0;
unsigned char g_frequencyL          =  0;
unsigned int  g_frequencyB;
double        g_frequency           = 94.3; // default Dutch FM NPO4
//
//

// -- related to networking
// -- use static IP addressing
IPAddress g_staticIP(192,168,0,212);
IPAddress g_gateway(192,168,0,1);
IPAddress g_subnet(255,255,255,0);

// -- related to remote control
// 
#define RC_P_SIZE 255
char          g_remote_control_packet[RC_P_SIZE]; //buffer to hold incoming packet
unsigned int  g_local_port = 8888;                // local port to listen for UDP packets / remote control

void setup() {
  Serial.begin(9600);
  delay(1000);    // Give serial time to open
  Serial.println("\nsetup|wekker|serial begins.");
  Wire.begin();
  g_pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  g_pixels.clear();
  g_pixels.show();
  Serial.println("setup|reinit devices done.");
	express_state(WKST_ATTENTIVE);
	delay(100);
  Serial.println("setup|setup done.");
  setup_network_and_time();
}

void setup_network_and_time() {
  // set notification call-back function
  sntp_set_time_sync_notification_cb( time_available_callback );

  /**
   * NTP server address could be aquired via DHCP,
   *
   * NOTE: This call should be made BEFORE esp32 aquires IP address via DHCP,
   * otherwise SNTP option 42 would be rejected by default.
   * NOTE: configTime() function call if made AFTER DHCP-client run
   * will OVERRIDE aquired NTP server address
   */
  //sntp_servermode_dhcp(1);    // (optional)

  /**
   * This will set configured ntp servers and constant TimeZone/daylightOffset
   * should be OK if your time zone does not need to adjust daylightOffset twice a year,
   * in such a case time adjustment won't be handled automagicaly.
   */
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

  /**
   * A more convenient approach to handle TimeZones with daylightOffset 
   * would be to specify a environmnet variable with TimeZone definition including daylight adjustmnet rules.
   * A list of rules for your zone could be obtained from https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
   */
  //configTzTime(time_zone, ntpServer1, ntpServer2);

  //connect to WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  WiFi.config(g_staticIP, g_gateway, g_subnet);         // use static IP
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println(" CONNECTED");
  Serial.print("My IP number is ");
  Serial.println(WiFi.localIP());
#ifdef BENZASYNCUDP
  Serial.println("Starting Async UDP");
  do_async_udp();
#endif
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("No time available (yet)");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void adjustLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }
  g_cur_time_h         = timeinfo.tm_hour; // 0..23
  g_cur_time_m         = timeinfo.tm_min; // 0..59
  g_cur_time_s         = timeinfo.tm_sec; // 0..59
}

// Callback function (get's called when time adjusts via NTP)
void time_available_callback(struct timeval *t)
{
  //Serial.println("Got time adjustment from NTP!");
  printLocalTime();
  adjustLocalTime();
}

void loop() {
  while(1) { // forever
    g_cur_app_event         = detect_event(g_current_app_state, g_time_elapsed_in_state);
    if (g_cur_app_event != WKEV_NONE) {
      g_previous_app_state  = g_current_app_state;
      g_current_app_state   = exec_fsm(g_cur_app_event, g_previous_app_state);
      if (g_current_app_state != g_previous_app_state) {
        print_transition(g_previous_app_state, g_current_app_state);
        g_time_elapsed_in_state = 0;
        handle_state_transition(g_previous_app_state, g_current_app_state);
      }
    }
    g_time_elapsed_in_state++;
    handle_current_state(g_current_app_state, g_time_elapsed_in_state);
    handle_always(g_time_elapsed_in_state);
    delay(100); // ms
  }
}

void handle_always(unsigned long elapsed) {
  // gets here always every 100ms
  if  ((elapsed % 10) == 0) execute_simple_inaccurate_clock(); // keeps inaccurate current time in application globals
  if (((elapsed % 10) == 0) && g_show_clock_anyway) show_simple_inaccurate_clock(g_cur_time_h, g_cur_time_m, g_cur_time_s, false);
}

unsigned short detect_event(unsigned short in_state, unsigned long elapsed) {
  unsigned short rc_event;
  rc_event = detect_remote_control_event();
  if (rc_event != WKEV_NONE) {
    g_rc_event = WKEV_NONE; // reset the remote event
    return rc_event; // holds the current event
  }
  switch(in_state) {
    case WKST_STOPPED:
      if (detected_startstop_touched())             return WKEV_STARTSTOP_TOUCHED;
      if (detected_attention_touched())             return WKEV_ATTENTION_TOUCHED;
      break;
    case WKST_STARTED:      
      if (detected_timer_expiry(in_state, elapsed)) return WKEV_TIMER_EXPIRED;
      if (detected_startstop_touched())             return WKEV_STARTSTOP_TOUCHED;
      break;
    case WKST_ALARMING:     
      if (detected_timer_expiry(in_state, elapsed)) return WKEV_TIMER_EXPIRED;
      if (detected_startstop_touched())             return WKEV_STARTSTOP_TOUCHED;
      if (detected_snoozed_touched())               return WKEV_SNOOZE_TOUCHED;
      break;
    case WKST_SNOOZED:      
      if (detected_timer_expiry(in_state, elapsed)) return WKEV_TIMER_EXPIRED;
      if (detected_startstop_touched())             return WKEV_STARTSTOP_TOUCHED;
      break;
    case WKST_ATTENTIVE:    
      if (detected_timer_expiry(in_state, elapsed)) return WKEV_TIMER_EXPIRED;
      if (detected_attention_touched())             return WKEV_ATTENTION_TOUCHED;
      break;
    case WKST_WAKEING:       
    case WKST_UNKNOWN:                              return WKEV_POWERUP;
    default:                        
      return WKEV_NONE;
  }
  return WKEV_NONE;
}

boolean detected_timer_expiry(unsigned short in_state, unsigned long elapsed) {
  switch(in_state) {
    case WKST_STARTED:
      if ((elapsed%100) == 0) {
        // Serial.println("Check alarm");
      }
      return ((g_cur_time_h == g_alarm_time_h) && (g_cur_time_m == g_alarm_time_m)); 
    case WKST_ALARMING:
      return (elapsed > g_alarming_duration);  
    case WKST_SNOOZED:
      return (elapsed > g_snooze_duration);  
    case WKST_ATTENTIVE:
      return (elapsed > g_attentive_duration); 
    case WKST_STOPPED:
    case WKST_WAKEING:
    case WKST_UNKNOWN:
      return false;
  }
  return false;
}

boolean detected_startstop_touched() {
  return (touchRead(T5) < T5_TOUCH_SENSITIVITY);        // get value using T5 - left
}

boolean detected_snoozed_touched() {
  return (touchRead(T7) < T7_TOUCH_SENSITIVITY);        // get value using T7 - middle
}

boolean detected_attention_touched() {
  return (touchRead(T8) < T8_TOUCH_SENSITIVITY);        // get value using T8 - right
}

unsigned short exec_fsm(unsigned short event, unsigned short prev_state) {
  //Serial.println("exec_fsm|exec FSM:");
  print_fsm_entry(event, prev_state);
  switch(g_previous_app_state) {
    case WKST_STOPPED:
      switch(event) {
        case WKEV_STARTSTOP_TOUCHED:  return WKST_STARTED;
        case WKEV_ATTENTION_TOUCHED:  return WKST_ATTENTIVE;
        default:                      return WKST_STOPPED;
      }
    case WKST_STARTED:
      switch(event) {
        case WKEV_TIMER_EXPIRED:      return WKST_ALARMING;
        case WKEV_ATTENTION_TOUCHED:  return WKST_STARTED; 
        case WKEV_STARTSTOP_TOUCHED:  
        default:                      return WKST_STOPPED; 
      }
    case WKST_SNOOZED:
      switch(event) {
        case WKEV_TIMER_EXPIRED:      return WKST_ALARMING; 
        case WKEV_ATTENTION_TOUCHED:  return WKST_SNOOZED;
        case WKEV_STARTSTOP_TOUCHED:  return WKST_STOPPED;
        default:                      return WKST_SNOOZED;
      }
      break;
    case WKST_ALARMING:
      switch(event) {
        case WKEV_SNOOZE_TOUCHED:     return WKST_SNOOZED;
        case WKEV_TIMER_EXPIRED:      return WKST_STOPPED;
        case WKEV_STARTSTOP_TOUCHED:  return WKST_STOPPED;
        case WKEV_ATTENTION_TOUCHED:
        default:                      return WKST_ALARMING; 
      }
    case WKST_ATTENTIVE:
      switch(event) {
        case WKEV_ATTENTION_TOUCHED:  return WKST_STOPPED;
        case WKEV_TIMER_EXPIRED:      return WKST_STOPPED;
        case WKEV_STARTSTOP_TOUCHED:
        case WKEV_SNOOZE_TOUCHED:
        default:                      return WKST_ATTENTIVE;
      }
    case WKST_WAKEING:
    case WKST_UNKNOWN:
    default:                          return WKST_STOPPED; 
  } 
}

void handle_state_transition(unsigned short previous, unsigned short next) {
  switch(previous) {
    case WKST_STOPPED:
      switch(next) {
        case WKST_STARTED:    
          express_state(WKST_STARTED);    
          mute();
          //
          // DEBUG only
          // Set the alarm time 1 minutes from now.
          //
          //g_alarm_time_h = g_cur_time_h;
          //g_alarm_time_m = (g_cur_time_m+1)%60;
          //
          break;
        case WKST_ATTENTIVE:  
          express_state(WKST_ATTENTIVE);  
          mute(); 
          enable_simple_inaccurate_clock(true);
          break;
      }
      break;
    case WKST_STARTED:      
      switch(next) {
        case WKST_ALARMING:   
          express_state(WKST_ALARMING); 
          unmute(); 
          enable_simple_inaccurate_clock(false);
          break;
        case WKST_STOPPED:    express_state(WKST_STOPPED);    mute();  break;
      }
      break;
    case WKST_SNOOZED:      
      switch(next) {
        case WKST_ALARMING:   
          express_state(WKST_ALARMING); 
          unmute(); 
          enable_simple_inaccurate_clock(false);
          break;
        case WKST_STOPPED:    express_state(WKST_STOPPED);    mute(); break;
      }
      break;
    case WKST_ALARMING:     
      switch(next) {
        case WKST_STOPPED:    express_state(WKST_STOPPED);    mute(); break;
        case WKST_SNOOZED:    express_state(WKST_SNOOZED);    mute(); break;
      }
      break;
    case WKST_ATTENTIVE:    
      switch(next) {
        case WKST_STOPPED:    
          express_state(WKST_STOPPED);    
          mute(); 
          if (!g_show_clock_anyway) disable_simple_inaccurate_clock();
          break;
      }
      break;
    case WKST_WAKEING:       
    case WKST_UNKNOWN:      
    default:                  express_state(WKST_STOPPED);    mute();
  }
}

void handle_current_state(unsigned short current_state, unsigned short elapsed) {
  switch(current_state) {
    case WKST_STARTED:    break;     
    case WKST_STOPPED:    break;     
    case WKST_SNOOZED:    break;     
    case WKST_ALARMING:   do_alarming(elapsed);  break;    
    case WKST_ATTENTIVE:  do_attentive(elapsed); break;
    case WKST_UNKNOWN:         
    case WKST_WAKEING:        
    default:
      break;
  }
}

void print_fsm_entry(unsigned short event, unsigned short prev_state) {
  Serial.print("event: ");
  switch(event) {
    case WKEV_UNKNOWN: Serial.print("UNKNOWN"); break;
    case WKEV_TIMER_EXPIRED: Serial.print("TIMER_EXPIRED"); break;
    case WKEV_STARTSTOP_TOUCHED: Serial.print("STARTSTOP_TOUCHED"); break;
    case WKEV_SNOOZE_TOUCHED: Serial.print("SNOOZE_TOUCHED"); break;
    case WKEV_ATTENTION_TOUCHED: Serial.print("ATTENTION_TOUCHED"); break;
    case WKEV_SPURIOUS: Serial.print("SPURIOUS"); break;
    default: Serial.print("SPURIOUS");
  }
  Serial.print(" previous: ");
  switch(prev_state) {
    case WKST_UNKNOWN:    Serial.println("UNKNOWN"); break;
    case WKST_WAKEING:    Serial.println("WAKEING"); break;
    case WKST_STARTED:    Serial.println("STARTED"); break;
    case WKST_STOPPED:    Serial.println("STOPPED"); break;
    case WKST_SNOOZED:    Serial.println("SNOOZED"); break;
    case WKST_ALARMING :  Serial.println("ALARMING"); break;
    case WKST_ATTENTIVE:  Serial.println("ATTENTIVE"); break;
    default:              Serial.println("UNKNOWN");
  }
}

void print_transition(unsigned short from_state, unsigned short to_state) {
  Serial.print("transition from: ");
  switch(from_state) {
    case WKST_UNKNOWN:    Serial.print("UNKNOWN"); break;
    case WKST_WAKEING:    Serial.print("WAKEING"); break;
    case WKST_STARTED:    Serial.print("STARTED"); break;
    case WKST_STOPPED:    Serial.print("STOPPED"); break;
    case WKST_SNOOZED:    Serial.print("SNOOZED"); break;
    case WKST_ALARMING :  Serial.print("ALARMING"); break;
    case WKST_ATTENTIVE:  Serial.print("ATTENTIVE"); break;
    default:              Serial.print("UNKNOWN");
  }
  Serial.print(" to: ");
  switch(to_state) {
    case WKST_UNKNOWN:    Serial.print("UNKNOWN"); break;
    case WKST_WAKEING:    Serial.print("WAKEING"); break;
    case WKST_STARTED:    Serial.print("STARTED"); break;
    case WKST_STOPPED:    Serial.print("STOPPED"); break;
    case WKST_SNOOZED:    Serial.print("SNOOZED"); break;
    case WKST_ALARMING :  Serial.print("ALARMING"); break;
    case WKST_ATTENTIVE:  Serial.print("ATTENTIVE"); break;
    default:              Serial.print("UNKNOWN");
  }
  Serial.println(".");
}

void do_attentive(unsigned short elapsed) {
  // this function is called every 100 ms ....
  // 
  // do whatever needs to be done on a regular basis in this state ....
  //
  if (((elapsed % 10) == 0) && (!g_show_clock_anyway))  show_simple_inaccurate_clock(g_cur_time_h, g_cur_time_m, g_cur_time_s, true);
}

void do_alarming(unsigned short elapsed) {
  // this function is called every 100 ms ....
  // 
  // do whatever needs to be done on a regular basis in this state ....
  //
  if (((elapsed % 10) == 0) && (!g_show_clock_anyway))  show_simple_inaccurate_clock(g_cur_time_h, g_cur_time_m, g_cur_time_s, false);
}

// =====================================================================================
// Detect and handle remote control commands
// =====================================================================================
unsigned short detect_remote_control_event() {
  // Note that several events within the regualr delay of 100ms will not be
  // detected seperately.
  if (g_rc_event != WKEV_NONE) {
    return g_rc_event;
  }
  return WKEV_NONE;
}

#ifdef BENZASYNCUDP
void do_async_udp() {
    if(Udp.listen(g_local_port)) {
        Serial.print("UDP Listening on IP: ");
        Serial.println(WiFi.localIP());
        Udp.onPacket([](AsyncUDPPacket packet) {
            Serial.print("UDP Packet Type: ");
            Serial.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
            Serial.print(", From: ");
            Serial.print(packet.remoteIP());
            Serial.print(":");
            Serial.print(packet.remotePort());
            Serial.print(", To: ");
            Serial.print(packet.localIP());
            Serial.print(":");
            Serial.print(packet.localPort());
            Serial.print(", Length: ");
            Serial.print(packet.length());
            Serial.print(", Data: ");
            Serial.write(packet.data(), packet.length());
            Serial.println();
            int l = packet.length();
            handle_remote_control_command(packet.data(), l );
            //reply to the client
            //packet.printf("Got %u bytes of data", packet.length());
            
        });
    } else {
      Serial.println("UDP.Listen not succesful.");      
    }
}
#endif

void handle_remote_control_command(unsigned char * cmd, int len) {
  char hm[6];
  unsigned short sync_h;
  unsigned short sync_m;
  unsigned short a_period;
  float f;
  if (len >= 1) {
    switch (cmd[0]) {
      case 'L': // remote attentive touch
        g_rc_event = WKEV_ATTENTION_TOUCHED;
        if (benz_verbose) {
          Serial.println("L");
        }
      break;
      case 'N': // remote snooze touch
        g_rc_event = WKEV_SNOOZE_TOUCHED;
        if (benz_verbose) {
          Serial.println("N");
        }
      break;
      case 'O': // remote start/stop touch
        g_rc_event = WKEV_STARTSTOP_TOUCHED ;
        if (benz_verbose) {
          Serial.println("O");
        }
      break;
      case 'D': // remote set clock on
        g_show_clock_anyway = true;
        enable_simple_inaccurate_clock(true); 
        if (benz_verbose) {
          Serial.println("D");
        }
      break;
      case 'E': // remote set clock off
        disable_simple_inaccurate_clock(); 
        g_show_clock_anyway = false;
        if (benz_verbose) {
          Serial.println("E");
        }
      break;
      case 'F': // remote set show seconds
        g_show_seconds_anyway = true;
        if (benz_verbose) {
          Serial.println("F");
        }
      break;
      case 'G': // remote set do not show seconds
        g_show_seconds_anyway = false;
        if (benz_verbose) {
          Serial.println("G");
        }
      break;
      case 'M': // mute the radio
        if (benz_verbose) {
          Serial.println("M");
        }
        mute();
      break;
      case 'U': // unmute the radio
        if (benz_verbose) {
          Serial.println("U");
        }
        unmute();
      break;
      case 'P': // set alarm hours
        if (benz_verbose) {
          Serial.println("P");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        sync_h = atoi(hm);
        Serial.println(sync_h);
        if (cmd[3] == '|') {
          for (int i=0; i<(len-3); i++) {
            hm[i] = cmd[4+i];
            hm[i+1] = (char)0;
          } 
        } else {
          if (cmd[4] == '|') {
            for (int i=0; i<(len-4); i++) {
              hm[i] = cmd[5+i];
              hm[i+1] = (char)0;
            } 
          }
        }
        sync_m = atoi(hm);
        Serial.println(sync_m);
        g_alarm_time_h = sync_h;
        g_alarm_time_m = sync_m;
      break;
      case 'R': // set current hour and minutes
        if (benz_verbose) {
          Serial.println("R");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        sync_h = atoi(hm);
        Serial.println(sync_h);
        if (cmd[3] == '|') {
          for (int i=0; i<(len-3); i++) {
            hm[i] = cmd[4+i];
            hm[i+1] = (char)0;
          } 
        } else {
          if (cmd[4] == '|') {
            for (int i=0; i<(len-4); i++) {
              hm[i] = cmd[5+i];
              hm[i+1] = (char)0;
            } 
          }
        }
        sync_m = atoi(hm);
        Serial.println(sync_m);
        g_cur_time_h = sync_h;
        g_cur_time_m = sync_m;
      break;
      case 'A': // set alarmduur
        if (benz_verbose) {
          Serial.println("A");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        a_period = atoi(hm);
        Serial.println(a_period);
        g_alarming_duration = a_period * 600;
      break;
      case 'B': // set snooze duration
        if (benz_verbose) {
          Serial.println("B");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        a_period = atoi(hm);
        Serial.println(a_period);
        g_snooze_duration = a_period * 600;
      break;
      case 'C': // set attentive duration
        if (benz_verbose) {
          Serial.println("C");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        a_period = atoi(hm);
        Serial.println(a_period);
        g_attentive_duration = a_period * 600;
      break;
      case 'I': // set radio frequency
        if (benz_verbose) {
          Serial.println("I");
        }
        for (int i=0; i<(len-2); i++) {
          hm[i] = cmd[2+i];
          hm[i+1] = (char)0;
        }
        f = atof(hm) / 10.0;
        Serial.println(f);
        g_frequency = f;
        setFrequency();
      break;
      default:
      break;
    }
  }
}

// =====================================================================================
// Stuff to express the current state by pixels and radio
// =====================================================================================
void express_state(unsigned short state) {
  
  // return; // debugging for now ....
  //Serial.print("express_state: ");
  //Serial.println(state);
  
#define STATEPXLINDEX CENTREPIXELIDX
  g_pixels.clear();
  g_pixels.show();
  switch(state) {
    case WKST_UNKNOWN:
      pixel_on(STATEPXLINDEX,   16,    16,    16); // dark white
      break;
    case WKST_WAKEING:
      pixel_on(STATEPXLINDEX,   0,    0,    1); // dark blue
      break;
    case WKST_STOPPED:
      pixel_on(STATEPXLINDEX,   0,    1,    0); // dark green
      break;
    case WKST_STARTED:
      pixel_on(STATEPXLINDEX,   1,    0,    0); // dark red
      break;
    case WKST_SNOOZED:
      pixel_on(STATEPXLINDEX,   8,    2,    0); // soft orange 
      break;
    case WKST_ALARMING:
      pixel_on(STATEPXLINDEX,  64,    0,    64); // bright purple
      break;
    case WKST_ATTENTIVE:
      pixel_on(STATEPXLINDEX,   1,    1,     1); //  white
      break;
    default:
      pixel_off(STATEPXLINDEX);
  }
  g_pixels.show();
}


// =====================================================================================
// Radio functions
// =====================================================================================
void setFrequency()
{
  if (g_radio_state) {
    unmute(); 
  } else {
    mute();
  }
}

void mute() {
  g_radio_state = false;
  g_frequencyB = 4 * (g_frequency * 1000000 + 225000) / 32768;
  g_frequencyH = g_frequencyB >> 8;
  g_frequencyL = g_frequencyB & 0XFF;
	g_frequencyH |= 0b10000000; // OFF - MUTED
  delay(100);
  Wire.beginTransmission(0x60);
  Wire.write(g_frequencyH);
  Wire.write(g_frequencyL);
  Wire.write(0xB8); // On | Mono
  //Wire.write(0x10);
  Wire.write(0x50); // Standby On
  Wire.write((byte)0x00);
  Wire.endTransmission();
  delay(100); 
}

void unmute() {
  g_radio_state = true;
  g_frequencyB = 4 * (g_frequency * 1000000 + 225000) / 32768;
  g_frequencyH = g_frequencyB >> 8;
  g_frequencyL = g_frequencyB & 0XFF;
	g_frequencyH &= 0b01111111; // ON - UNMUTED
  delay(100);
  Wire.beginTransmission(0x60);
  Wire.write(g_frequencyH);
  Wire.write(g_frequencyL);
  Wire.write(0xB8); // On | Mono
  Wire.write(0x10);
  Wire.write((byte)0x00);
  Wire.endTransmission();
  delay(100); 
}
// =====================================================================================

// =====================================================================================
// Additional pixel functions
// =====================================================================================
void pixel_on(unsigned p, unsigned r, unsigned g, unsigned b) {
    // g_pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    g_pixels.setPixelColor(p, g_pixels.Color(r,g,b));
    g_pixels.show();
}

void pixel_off(unsigned p) {
    g_pixels.setPixelColor(p, g_pixels.Color(0,0,0));
    g_pixels.show();
}
// =====================================================================================


// =====================================================================================
// Clock functions
// =====================================================================================
int hours2index(unsigned h) {
  if (h>24) return 0; else return HOURS_RING_IDX + (h%12); // modulo 12
}

int minutes2index(unsigned m) {
  if (m>60) return 0; else return MINUTES_RING_IDX + 2*(m%60)/5; // modulo 60, 2,5 minute per pixel
}

int seconds2index(unsigned s) {
  if (s>60) return 0; else return TENSECONDS_RING_IDX + (s%60)/10; // modulo 60, 10 seconds per pixel
}

void all_hms_pixels_off() {
  for (unsigned short s=0;s<60;s++) {
    g_pixels.setPixelColor(seconds2index(s), g_pixels.Color(0,0,0));
  }
  for (unsigned short m=0;m<60;m++) {
    g_pixels.setPixelColor(minutes2index(m), g_pixels.Color(0,0,0));
  }
  for (unsigned short h=0;h<24;h++) {
    g_pixels.setPixelColor(hours2index(h), g_pixels.Color(0,0,0));
  }
  g_pixels.show();
}

void execute_simple_inaccurate_clock() {
  unsigned short oh, om, os, nh, nm, ns;
  oh = g_cur_time_h;
  om = g_cur_time_m;
  os = g_cur_time_s;
  nh = oh;
  nm = om;
  ns = os;
  ns = (os+1)%60;
  if (os == 59) { // minutes increment
    nm = (om+1)%60;
    if (om == 59) { //hours increment
      nh = (oh+1)%24;
    }
    //Serial.print("Minute: ");
    //Serial.println(g_cur_time_m);
  }
  // Save time value in global variables!
  g_cur_time_s = ns;
  g_cur_time_m = nm;
  g_cur_time_h = nh;
}

unsigned short g_ohi = 0; // index of current hour pixel
unsigned short g_omi = 0; // index of current minute pixel
unsigned short g_osi = 0; // index of current second pixel

void enable_simple_inaccurate_clock(boolean with_s) {
  printLocalTime();     // it will take some time to sync time :)
  adjustLocalTime();
  g_osi = seconds2index(g_cur_time_s);
  g_omi = minutes2index(g_cur_time_m);
  g_ohi = hours2index(g_cur_time_h);
  if (with_s || g_show_seconds_anyway) {
    g_pixels.setPixelColor(g_osi, g_pixels.Color(random(8), random(8), random(8)));  // turn current seconds on
  }
  g_pixels.setPixelColor(g_omi, g_pixels.Color(random(8), random(8), random(8)));  // turn current minutes on
  g_pixels.setPixelColor(g_ohi, g_pixels.Color(random(8), random(8), random(8)));    // turn current hour on
  g_pixels.show();
}

void disable_simple_inaccurate_clock() {
  all_hms_pixels_off();
}

void show_simple_inaccurate_clock(unsigned short h, unsigned short m, unsigned short s, boolean with_s) {
  unsigned short hi, mi,  si;
  hi = hours2index(h);
  mi = minutes2index(m);
  si = seconds2index(s);
  if (g_ohi != hi) {
    g_pixels.setPixelColor(g_ohi, g_pixels.Color(0,0,0)); // turn old hours off
    g_pixels.setPixelColor(hi, g_pixels.Color(random(8), random(8), random(8))); // turn current hours on
    g_ohi = hi;
  }  
  if (g_omi != mi) {
    g_pixels.setPixelColor(g_omi, g_pixels.Color(0,0,0)); // turn old minutes off
    g_pixels.setPixelColor(mi, g_pixels.Color(random(8), random(8), random(8))); // turn current minutes on
    g_omi = mi;
  }  
  if (g_osi != si) {
    g_pixels.setPixelColor(g_osi, g_pixels.Color(0,0,0)); // turn old seconds off
    if (with_s || g_show_seconds_anyway) {
      g_pixels.setPixelColor(si, g_pixels.Color(random(8), random(8), random(8))); // turn current seconds on
    }
    g_osi = si;
  }
  g_pixels.show();
}
