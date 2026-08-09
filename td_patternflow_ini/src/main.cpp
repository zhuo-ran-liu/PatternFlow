#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

WiFiUDP udp;

#define VIDEO_PORT 8888

#define WIFI_SSID "MERCUSYS_4769"
#define WIFI_PASSWORD "60128239"

// Source resolution (what's sent over UDP)
#define SRC_WIDTH  64
#define SRC_HEIGHT 32

// Display resolution (physical panel)
#define MATRIX_WIDTH  128
#define MATRIX_HEIGHT 64

// Scale factor (how many display pixels per source pixel, per axis)
#define SCALE_X (MATRIX_WIDTH  / SRC_WIDTH)
#define SCALE_Y (MATRIX_HEIGHT / SRC_HEIGHT)

#include "config.h"
#include "core_display.h"
#include "inputs_osc.h"

MatrixPanel_I2S_DMA *dma_display = nullptr;

#define FRAME_SIZE (SRC_WIDTH * SRC_HEIGHT * 3)
#define CHUNK 1020

// Header layout: [frameId:1][packetIndex:2][totalPackets:2] = 5 bytes
#define HEADER_SIZE 5
#define UDP_BUF_SIZE (CHUNK + HEADER_SIZE)

// Stuck-frame watchdog: if a frame hasn't completed within this window,
// abandon it so a single dropped packet can't hang the receiver forever.
#define FRAME_TIMEOUT_MS 200

uint8_t frameBuffer[FRAME_SIZE];
uint8_t udpBuffer[UDP_BUF_SIZE];

uint16_t expectedTotalPackets = 0;
uint16_t receivedPackets = 0;
bool frameInProgress = false;
uint8_t currentFrameId = 0;
uint32_t frameStartMs = 0;

void initWiFi()
{
  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_STA);

  // Disable modem power-save so the radio doesn't periodically sleep
  // and miss incoming UDP packets. This is the fix for the "smooth,
  // then periodic stutter" pattern.
  WiFi.setSleep(false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void initUDP()
{
  if (udp.begin(VIDEO_PORT))
  {
    Serial.println("UDP OK");
  }
  else
  {
    Serial.println("UDP FAILED");
  }
}

void displayFrame()
{
  int idx = 0;

  for (int sy = 0; sy < SRC_HEIGHT; sy++)
  {
    for (int sx = 0; sx < SRC_WIDTH; sx++)
    {
      uint8_t r = frameBuffer[idx++];
      uint8_t g = frameBuffer[idx++];
      uint8_t b = frameBuffer[idx++];

      int dx0 = sx * SCALE_X;
      int dy0 = sy * SCALE_Y;

      for (int oy = 0; oy < SCALE_Y; oy++)
      {
        for (int ox = 0; ox < SCALE_X; ox++)
        {
          dma_display->drawPixelRGB888(
              dx0 + ox,
              dy0 + oy,
              r, g, b);
        }
      }
    }
  }

  dma_display->flipDMABuffer();
}

// Processes exactly one waiting UDP packet, if any.
// Returns true if a packet was read (caller should keep draining),
// false if there was nothing to read (caller should stop).
bool processOnePacket()
{
  int packetSize = udp.parsePacket();

  if (packetSize <= 0)
  {
    return false; // nothing waiting
  }

  if (packetSize <= HEADER_SIZE)
  {
    // still need to consume it so it doesn't sit in the socket buffer
    udp.read(udpBuffer, UDP_BUF_SIZE);
    return true;
  }

  if (packetSize > UDP_BUF_SIZE)
  {
    // drop oversized packet, still clear socket buffer
    udp.read(udpBuffer, UDP_BUF_SIZE);
    return true;
  }

  int len = udp.read(udpBuffer, UDP_BUF_SIZE);

  if (len < HEADER_SIZE)
  {
    return true;
  }

  uint8_t frameId = udpBuffer[0];
  uint16_t packetIndex;
  uint16_t totalPackets;

  memcpy(&packetIndex, udpBuffer + 1, 2);
  memcpy(&totalPackets, udpBuffer + 3, 2);

  int payloadLen = len - HEADER_SIZE;

  // start of a new frame
  if (packetIndex == 0)
  {
    expectedTotalPackets = totalPackets;
    receivedPackets = 0;
    frameInProgress = true;
    currentFrameId = frameId;
    frameStartMs = millis();
  }

  // Reject packets that don't belong to the frame we're currently
  // assembling -- prevents a late straggler from a previous frame
  // corrupting the buffer of the frame in progress.
  if (!frameInProgress ||
      frameId != currentFrameId ||
      totalPackets != expectedTotalPackets)
  {
    return true;
  }

  size_t offset = (size_t)packetIndex * CHUNK;

  if (offset + payloadLen > FRAME_SIZE)
  {
    // malformed / out-of-range packet, ignore
    return true;
  }

  memcpy(frameBuffer + offset, udpBuffer + HEADER_SIZE, payloadLen);

  receivedPackets++;

  if (receivedPackets >= expectedTotalPackets)
  {
    displayFrame();
    frameInProgress = false;
    Serial.println("FRAME DISPLAYED (SCALED)");
  }

  return true;
}

void receiveVideoFrame()
{
  // Drain everything currently waiting in the socket buffer instead of
  // handling one packet per loop() call. If the sender bursts several
  // packets close together, reading only one per loop lets a backlog
  // build up over the course of the frame.
  while (processOnePacket())
  {
    // keep going until the socket is empty
  }

  // Abandon a frame that's been in progress too long -- e.g. because a
  // packet got dropped on the network -- so a single lost packet can't
  // stall the receiver until the next frame's packet 0 happens to arrive.
  if (frameInProgress && (millis() - frameStartMs > FRAME_TIMEOUT_MS))
  {
    Serial.println("FRAME TIMEOUT - dropping incomplete frame");
    frameInProgress = false;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== UDP VIDEO STREAM (64x32 -> 128x64) ===");

  initDisplay();
  initWiFi();
  initUDP();
  initEncoders();
  initInputOsc();
}

void loop()
{
  receiveVideoFrame();
  updateInputsAndSendOsc(); // only sends OSC when a knob/button value changes
  delay(1);
  yield();
}