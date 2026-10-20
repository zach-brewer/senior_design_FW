/*
  fake_mic_sender.ino
  ---------------------------------------------------------------------------
  Sends FAKE microphone readings to the TDOA backend over wired Ethernet, so
  you can test the backend's networking (IP/port/UDP packet handling,
  arm/disarm gating, event correlation, the live website) without any real
  microphone hardware.

  Board:    Arduino GIGA R1 WiFi
  Network:  A W5500-based Ethernet shield/module (the GIGA R1 WiFi has no
            built-in wired Ethernet -- only onboard Wi-Fi/Bluetooth -- so a
            W5500 shield or breakout wired over SPI is required for this
            sketch). The backend itself doesn't care about the physical
            link -- it just listens for UDP/IP packets -- so everything this
            sketch proves out (IP, port, packet format, arm/disarm, the
            website updating live) is identical to what real mic hardware
            would need to do.

  Packet format sent, one per mic, per simulated shot:

      {"mic_id": <1-6>, "delta_us": <int, >= 0>}

    - delta_us: microseconds elapsed since whichever mic triggered first for
      this shot (that mic itself sends 0). There's no absolute/synchronized
      clock to worry about -- only the elapsed time from the trigger matters
      -- so this sketch just computes true propagation time to each mic and
      subtracts the smallest one, the same way the real mic boards measure
      elapsed time locally from a shared trigger line.

  There's no voltage field: on the real system, each mic's 0.2V trigger
  threshold is applied entirely on the downstream Nucleo board, which only
  reports a mic once it has actually crossed threshold. This backend
  requires a reading from all 6 mics before it will solve a shot, so this
  sketch always sends all 6.

  IMPORTANT -- the website must be ARMED to see anything:
    The backend drops all incoming UDP packets while the system is
    "disarmed" (this is intentional -- see README.md). Click "Arm system"
    on the website BEFORE this sketch starts sending, or you'll see nothing
    happen and wrongly think the network is broken.

  Before uploading, edit the "EDIT THESE" section below:
    - mac[]       any locally-unique MAC address (the default is fine on
                  most home/lab networks, but must be unique per device if
                  you ever run more than one).
    - arduinoIp   a free static IP on your network, same subnet as the PC
                  running the backend.
    - serverIp    the IP address of the PC running `python server.py`
                  (NOT 127.0.0.1/0.0.0.0 -- those only work talking to
                  yourself; use the PC's actual LAN IP, e.g. from
                  `ipconfig` on Windows or `ip addr` on Linux/Mac).
    - serverPort  must match backend/config.py's UDP_PORT (default 5005).

  Wiring notes:
    - A W5500 shield that plugs into the GIGA's Mega-footprint headers uses
      the ICSP/SPI pins automatically; CS defaults to pin 10 below, which is
      standard for most shields (change ETHERNET_CS_PIN if yours differs).
    - A bare W5500 breakout wired by jumpers needs: VCC -> 3.3V (NOT 5V --
      the GIGA's SPI pins are 3.3V logic), GND -> GND, SCK/MOSI/MISO -> the
      GIGA's SPI pins, and CS -> any free digital pin (set that pin number
      in ETHERNET_CS_PIN).

  Library required (Arduino IDE -> Library Manager):
    "Ethernet" by Arduino (the classic library, arduino-libraries/Ethernet,
    v2.0.0+). It auto-detects W5100/W5200/W5500 chips, so no chip-specific
    settings are needed beyond the CS pin.
  ---------------------------------------------------------------------------
*/

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

// =========================== EDIT THESE ====================================
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };  // any locally-unique MAC
IPAddress arduinoIp(192, 168, 1, 177);                // static IP for this Arduino
IPAddress serverIp(192, 168, 1, 50);                  // <-- PC running the python backend
const unsigned int serverPort = 5005;                 // must match backend/config.py UDP_PORT
const int ETHERNET_CS_PIN = 10;                       // SPI chip-select pin for the W5500
// =============================================================================

// Mic layout -- MUST match backend/config.py's MIC_POSITIONS_M. If you
// change the mic geometry on the backend, mirror the change here too.
const int NUM_MICS = 6;
const int micId[NUM_MICS]  = { 1, 2, 3, 4, 5, 6 };
const float micX[NUM_MICS] = { -1.0, 0.0, 1.0, -1.0, 0.0, 1.0 };
const float micY[NUM_MICS] = { 0.0, 0.0, 0.0, -0.1524, -0.1524, -0.1524 };

// Must match backend/config.py's DEFAULT_SPEED_OF_SOUND_MPS.
const float SPEED_OF_SOUND_MPS = 343.0;

// Must match backend/config.py's TARGET_CENTER_M and roughly RING_RADII_M[-1].
const float TARGET_CENTER_X = 0.0;
const float TARGET_CENTER_Y = 1.2;
const float MAX_RADIUS_M = 0.345;  // ~ RING_RADII_M[-1] (0.300m) * 1.15

const unsigned long SHOT_INTERVAL_MS = 3000;  // time between fake shots
const float JITTER_US = 5.0;                  // simulated timing noise, stddev in microseconds

EthernetUDP Udp;
unsigned long lastShotAt = 0;

// Rough Gaussian noise via Box-Muller, using the stdlib-free Arduino random().
float gaussianNoise(float stddev) {
  float u1 = (random(1, 10000)) / 10000.0;  // (0, 1)
  float u2 = (random(0, 10000)) / 10000.0;  // [0, 1)
  float z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
  return z0 * stddev;
}

void sendFakeShot() {
  // Random impact point near the target, same distribution as
  // tools/simulate_udp_sender.py's --random mode.
  float r = MAX_RADIUS_M * sqrt(random(0, 10000) / 10000.0);
  float theta = (random(0, 10000) / 10000.0) * 2.0 * PI;
  float shotX = TARGET_CENTER_X + r * cos(theta);
  float shotY = TARGET_CENTER_Y + r * sin(theta);

  Serial.print("Simulated shot at x=");
  Serial.print(shotX, 4);
  Serial.print(" y=");
  Serial.println(shotY, 4);

  // Real mic hardware reports delta_us already relative to whichever mic
  // triggered first -- there's no shared absolute clock to anchor to. So we
  // compute each mic's true travel time first, then subtract the smallest
  // one to get the same kind of delta a real device would send.
  float travelUs[NUM_MICS];
  float minTravelUs = -1.0;

  for (int i = 0; i < NUM_MICS; i++) {
    float dx = shotX - micX[i];
    float dy = shotY - micY[i];
    float dist = sqrt(dx * dx + dy * dy);
    float jitterUs = gaussianNoise(JITTER_US);
    travelUs[i] = (dist / SPEED_OF_SOUND_MPS) * 1.0e6 + jitterUs;
    if (minTravelUs < 0 || travelUs[i] < minTravelUs) minTravelUs = travelUs[i];
  }

  for (int i = 0; i < NUM_MICS; i++) {
    unsigned long deltaUs = (unsigned long)max(0.0f, travelUs[i] - minTravelUs);

    char buf[48];
    int len = snprintf(buf, sizeof(buf),
                        "{\"mic_id\":%d,\"delta_us\":%lu}",
                        micId[i], deltaUs);

    Udp.beginPacket(serverIp, serverPort);
    Udp.write((const uint8_t *)buf, len);
    Udp.endPacket();

    Serial.print("  mic ");
    Serial.print(micId[i]);
    Serial.print(": delta_us=");
    Serial.println(deltaUs);
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) { ; }  // wait briefly for USB serial, but don't hang if unplugged

  randomSeed(analogRead(A0));  // leave A0 floating for a noisy seed

  Ethernet.init(ETHERNET_CS_PIN);

  Serial.println("Starting Ethernet...");
  Ethernet.begin(mac, arduinoIp);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("ERROR: No W5500/Ethernet controller detected.");
    Serial.println("Check wiring and ETHERNET_CS_PIN.");
  }

  delay(1000);
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("WARNING: Ethernet link is down -- check the cable.");
  }

  Udp.begin(8888);  // local UDP port this Arduino sends from; arbitrary

  Serial.print("Arduino IP: ");
  Serial.println(Ethernet.localIP());
  Serial.print("Sending fake shots to ");
  Serial.print(serverIp);
  Serial.print(":");
  Serial.println(serverPort);
  Serial.println("Reminder: the website must be ARMED to process these packets.");
}

void loop() {
  if (millis() - lastShotAt >= SHOT_INTERVAL_MS) {
    lastShotAt = millis();
    sendFakeShot();
  }
}
