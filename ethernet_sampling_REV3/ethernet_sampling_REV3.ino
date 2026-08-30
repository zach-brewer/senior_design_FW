#include <Arduino_AdvancedAnalog.h>
#include <SPI.h>
#include <Ethernet.h>
#include <limits.h>

#define SAMPLE_RATE 1000000
#define BUFFER_SIZE 512
#define QUEUE_DEPTH 256
#define THRESHOLD 40000  //29500

#define NUM_CHANNELS 3
#define WINDOW 8192
#define CIRC_SIZE 32768
#define MAX_LAG 1000

AdvancedADC adc(A0, A1, A2);  // LEFT, MIDDLE, RIGHT

uint16_t circ1[CIRC_SIZE];
uint16_t circ2[CIRC_SIZE];
uint16_t circ3[CIRC_SIZE];

volatile int circ_index = 0;

uint16_t win1[WINDOW];
uint16_t win2[WINDOW];
uint16_t win3[WINDOW];

bool triggered = false;
bool running = false;
bool window_ready = false;
int capture_start = 0;

uint64_t total_samples = 0;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 2);  // Arduino IP
EthernetServer server(80);
EthernetClient client;

void setup() {
  Ethernet.begin(mac, ip);
  server.begin();
  if (!adc.begin(AN_RESOLUTION_16, SAMPLE_RATE, BUFFER_SIZE, QUEUE_DEPTH)) {
    while (1)
      //Serial.println("a");
      ;
  }
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    Serial.read();

    if(c == '1'){
      send_arrays();
    }
    if(c == 'r'){
      reset_program();
      running = true;
    }
  } 
  EthernetClient newClient = server.available();
  if (newClient) {
    client = newClient;
  }
  if (client && client.available()) {
    char incomming = client.read();
    Serial.println(incomming);
    if(incomming == 'o'){
        reset_program();
        running = true;
    }
  }

  if (running) {
    SampleBuffer buf = adc.read();
    process_buffer(buf);

    if (window_ready) {
      running = false;
      send_arrays();
    }
  }
}

void process_buffer(SampleBuffer buf) {

  static uint16_t prev_mid = 0;
  //Serial.println(buf.size());
  for (int i = 0; i < buf.size(); i += NUM_CHANNELS) {

    uint16_t s1 = buf[i];
    uint16_t s2 = buf[i + 1];
    uint16_t s3 = buf[i + 2];

    // --- store in circular buffer ---
    circ1[circ_index] = s1;
    circ2[circ_index] = s2;
    circ3[circ_index] = s3;

    // --- trigger on middle channel ---
    if (!triggered && prev_mid < THRESHOLD && s1 >= THRESHOLD) {
      triggered = true;
      // capture window centered around trigger
      capture_start = circ_index;
      if (capture_start < 0) {
        capture_start += CIRC_SIZE;
      }
    }

    prev_mid = s1;
    // advance circular index
    circ_index++;
    if (circ_index >= CIRC_SIZE) circ_index = 0;

    // --- extract window once enough samples collected ---
    if (triggered && !window_ready) {

      // Wait until we have enough samples after trigger
      int diff = circ_index - capture_start;
      if (diff < 0) diff += CIRC_SIZE;

      if (diff > WINDOW) {

        // Find peak in middle channel after trigger
        int peak_idx = find_peak_index(capture_start);

        // Center window on peak
        int start = peak_idx - WINDOW / 2;
        if (start < 0) start += CIRC_SIZE;

        // Copy aligned window
        for (int k = 0; k < WINDOW; k++) {
          int idx = (start + k) % CIRC_SIZE;
          win1[k] = circ1[idx];
          win2[k] = circ2[idx];
          win3[k] = circ3[idx];
        }

        window_ready = true;
      }
    }
  }

  total_samples += buf.size() / NUM_CHANNELS;
  buf.release();
}

int find_peak_index(int start_idx) {
  int peak_idx = start_idx;
  uint16_t peak_val = 0;

  for (int i = 0; i < WINDOW; i++) {
    int idx = (start_idx + i) % CIRC_SIZE;
    uint16_t val = abs(circ2[idx]);

    if (val > peak_val) {
      peak_val = val;
      peak_idx = idx;
    }
  }

  return peak_idx;
}


void faster_send(uint16_t *x, bool last_array) {
  int index = 0;
  char buffer[256];
  for (int i = 0; i < WINDOW; i++) {
    if (last_array && i == WINDOW - 1) {
      index += snprintf(&buffer[index], sizeof(buffer) - index, "%d", x[i]);
    } else {
      index += snprintf(&buffer[index], sizeof(buffer) - index, "%d,", x[i]);
    }
    if (index > sizeof(buffer - 16)) {
      client.write((uint8_t *)buffer, index);
      index = 0;
    }
  }
  if (index > 0) {
    client.write((uint8_t *)buffer, index);
  }
}

void send_arrays() {
    if (client && client.connected()) {
      faster_send(win1, false);
      faster_send(win2, false);
      faster_send(win3, false);
      client.print("x");
    }
}

void reset_window(uint16_t *x, uint16_t *y, uint16_t *z) {
  for (int i = 0; i < WINDOW; i++) {
    x[i] = 0;
    y[i] = 15000;
    z[i] = 30000;
  }
}

void reset_program() {
  reset_window(win1, win2, win3);
  for (int i = 0; i < CIRC_SIZE; i++) {
    circ1[i] = circ2[i] = circ3[i] = 0;
  }
  total_samples = 0;
  capture_start = 0;
  triggered = false;
  window_ready = false;
  circ_index = false;
  capture_start = false;
  total_samples = false;
  adc.stop();
  delay(5);
  if (!adc.begin(AN_RESOLUTION_16, SAMPLE_RATE, BUFFER_SIZE, QUEUE_DEPTH)) {
    while (1)
      ;
  }
  delay(5);
}