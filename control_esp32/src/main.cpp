#include <WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <MIDI.h>
#include <FS.h>
#include <SPIFFS.h>

#define SERIAL_DEBUG

#define DNS_PORT 53
#define OSC_IN_PORT 8000
#define OSC_OUT_PORT 9000

#define NUM_INPUT_CHANNELS 4
#define NUM_OUTPUT_CHANNELS 4
#define NUM_PWM_CHANNELS (NUM_INPUT_CHANNELS * NUM_OUTPUT_CHANNELS)
#define NUM_MIDI_MESSAGES 32
#define PWM_FREQUENCY 625000
#define PWM_RESOLUTION_BITS 7
#define GPIO_PWM0 32
#define GPIO_PWM1 33
#define GPIO_PWM2 25
#define GPIO_PWM3 26
#define GPIO_PWM4 27
#define GPIO_PWM5 14
#define GPIO_PWM6 12
#define GPIO_PWM7 13
#define GPIO_PWM8 15
#define GPIO_PWM9 4
#define GPIO_PWM10 16
#define GPIO_PWM11 17
#define GPIO_PWM12 5
#define GPIO_PWM13 18
#define GPIO_PWM14 19
#define GPIO_PWM15 21

// MIDI2 on UART2
HardwareSerial Serial2(2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);
// MIDI2-IN callback prototypes
void handleNoteOff(byte channel, byte note, byte velocity);
void handleNoteOn(byte channel, byte note, byte velocity);
void handleAfterTouchPoly(byte channel, byte note, byte pressure);
void handleControlChange(byte channel, byte number, byte value);
void handleProgramChange(byte channel, byte number);
void handleAfterTouchChannel(byte channel, byte pressure);
void handlePitchBend(byte channel, int bend);
void handleSystemExclusive(byte *array, unsigned size);




struct Patch {
  byte channel;
  byte bank;
  byte patch;
  byte in[NUM_INPUT_CHANNELS];
  byte in_cc[NUM_PWM_CHANNELS];
  byte out[NUM_INPUT_CHANNELS];
  byte out_cc[NUM_PWM_CHANNELS];
  byte mix[NUM_PWM_CHANNELS];
  byte mix_cc[NUM_PWM_CHANNELS];
  byte mute[NUM_PWM_CHANNELS];
  byte mute_cc[NUM_PWM_CHANNELS];
  uint32_t midi_messages[NUM_MIDI_MESSAGES];
};





// AP IP Address
IPAddress apIP(192, 168, 1, 1);

// DNS Server
DNSServer dnsServer;

// UDP for OSC
WiFiUDP oscUDP;
// OSC error state
OSCErrorCode error;
// OSC handler prototypes
void vol(OSCMessage &msg, int offset);

// PWM Pin/Channel mapping
int pwmPins[NUM_PWM_CHANNELS] = {
    GPIO_PWM0, GPIO_PWM1, GPIO_PWM2, GPIO_PWM3,
    GPIO_PWM4, GPIO_PWM5, GPIO_PWM6, GPIO_PWM7,
    GPIO_PWM8, GPIO_PWM9, GPIO_PWM10, GPIO_PWM11,
    GPIO_PWM12, GPIO_PWM13, GPIO_PWM14, GPIO_PWM15};

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.print("Setting up SPIFFS...");
  if (!SPIFFS.begin(true, "/spiffs", 10))
  {
    Serial.println("SPIFFS Mount Failed");
  }
  Serial.println("OK");

  Serial.print("Setting up WiFi AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("kxmx_dca");
  delay(100);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  Serial.print("OK - Gateway: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("Starting DNS Server...");
  // Return our IP for all requests
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("OK");

  Serial.print("Announcing ourselves via MDNS...");
  MDNS.begin("kxmx_dca");
  MDNS.addService("kxmx_dca_touchoscbridge._udp", "_touchoscbridge._udp", OSC_IN_PORT);
  Serial.println("OK");

  Serial.print("Starting listen for OSC on UDP port...");
  oscUDP.begin(OSC_IN_PORT);
  Serial.println(OSC_IN_PORT);

  Serial.print("Starting MIDI2...");
  MIDI2.begin(MIDI_CHANNEL_OMNI);
  MIDI2.setHandleNoteOff(handleNoteOff);
  MIDI2.setHandleNoteOn(handleNoteOn);
  MIDI2.setHandleAfterTouchPoly(handleAfterTouchPoly);
  MIDI2.setHandleControlChange(handleControlChange);
  MIDI2.setHandleProgramChange(handleProgramChange);
  MIDI2.setHandleAfterTouchChannel(handleAfterTouchChannel);
  MIDI2.setHandlePitchBend(handlePitchBend);
  MIDI2.setHandleSystemExclusive(handleSystemExclusive);
  Serial.println("OK");

  Serial.print("Initialize PWM Channels... ");
  for (int i = 0; i < NUM_PWM_CHANNELS; i++)
  {
    Serial.print(i);
    ledcSetup(i, PWM_FREQUENCY, PWM_RESOLUTION_BITS);
    ledcAttachPin(pwmPins[i], i);
    // Initally off...
    ledcWrite(i, 0);
    Serial.print(" ");
  }
  Serial.println("OK");

  Serial.println("Ready!");
}

void loop()
{
  MIDI2.read();
  dnsServer.processNextRequest();
  int size = oscUDP.parsePacket();
  if (size > 0)
  {
    OSCMessage msg;
    while (size--)
    {
      msg.fill(oscUDP.read());
    }
    if (!msg.hasError())
    {
      msg.route("/vol", vol);
    }
    else
    {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }
}

/////////// OSC Handlers //////////////////////////////////////////////////////
void vol(OSCMessage &msg, int offset)
{
  int val = (int)msg.getFloat(0);
  int row = -1;
  int col = -1;

  if (msg.match("/0", offset))
  {
    row = 0;
  }
  else if (msg.match("/1", offset))
  {
    row = 1;
  }
  else if (msg.match("/2", offset))
  {
    row = 2;
  }
  else if (msg.match("/3", offset))
  {
    row = 3;
  }

  if (msg.match("/0", offset + 2))
  {
    col = 0;
  }
  else if (msg.match("/1", offset + 2))
  {
    col = 1;
  }
  else if (msg.match("/2", offset + 2))
  {
    col = 2;
  }
  else if (msg.match("/3", offset + 2))
  {
    col = 3;
  }

  if (row >= 0 && row <= 3 && col >= 0 && col <= 3)
  {
    int channel = row * 4 + col;
    ledcWrite(channel, val);
    
#ifdef SERIAL_DEBUG
    Serial.print("parsed: /vol/");
    Serial.print(row);
    Serial.print("/");
    Serial.print(col);
    Serial.print(" (");
    Serial.print(channel);
    Serial.print(") = ");
    Serial.println(val);
#endif
  }
}
///////////////////////////////////////////////////////////////////////////////


/////////// MIDI Handlers /////////////////////////////////////////////////////
void handleNoteOff(byte channel, byte note, byte velocity)
{
}
void handleNoteOn(byte channel, byte note, byte velocity)
{
}
void handleAfterTouchPoly(byte channel, byte note, byte pressure)
{
}
void handleControlChange(byte channel, byte number, byte value)
{
}
void handleProgramChange(byte channel, byte number)
{
}
void handleAfterTouchChannel(byte channel, byte pressure)
{
}
void handlePitchBend(byte channel, int bend)
{
}
void handleSystemExclusive(byte *array, unsigned size)
{
}
///////////////////////////////////////////////////////////////////////////////