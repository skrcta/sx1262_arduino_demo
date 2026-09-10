/*
 * SX1262MB1DAS / Semtech E449V01A radio test
 *
 * Requires: RadioLib (https://github.com/jgromes/RadioLib)
 *
 * The default build is a MASTER ping/pong test. Flash this sketch to one
 * board as-is, then uncomment TEST_ROLE_SLAVE below and flash it to the
 * second board.
 */

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Hardware configuration: Semtech mbed/Arduino shield mapping
// ---------------------------------------------------------------------------

#define SX1262_SCK        13
#define SX1262_MISO       12
#define SX1262_MOSI       11
#define SX1262_NSS         7
#define SX1262_ANT_SW      8
#define SX1262_DIO1        5
#define SX1262_BUSY        3
#define SX1262_NRESET      A0

// The board uses the SX1262 RF path, a 32 MHz TCXO, and an RF switch.
constexpr float RADIO_FREQUENCY_MHZ = 866.0f;
constexpr float RADIO_BANDWIDTH_KHZ = 125.0f;
constexpr uint8_t RADIO_SPREADING_FACTOR = 9;
constexpr uint8_t RADIO_CODING_RATE = 7;       // 4/7
constexpr uint8_t RADIO_SYNC_WORD = 0x12;      // private LoRa network
constexpr int8_t RADIO_TX_POWER_DBM = 10;
constexpr uint16_t RADIO_PREAMBLE_SYMBOLS = 8;
constexpr float RADIO_TCXO_VOLTAGE = 1.6f;
constexpr bool RADIO_USE_LDO = false;          // E449V01A DCC_SW uses L7

// ---------------------------------------------------------------------------
// Test selection
// ---------------------------------------------------------------------------

enum TestMode : uint8_t {
  MODE_INIT_ONLY,
  MODE_TX_ONLY,
  MODE_RX_ONLY,
  MODE_PING_PONG
};

// Change this to MODE_INIT_ONLY, MODE_TX_ONLY, or MODE_RX_ONLY for staged
// bring-up. The default exercises the two-board functional test.
#define TEST_MODE MODE_PING_PONG

// Leave TEST_ROLE_MASTER defined for the master. For the second board,
// comment it out and define TEST_ROLE_SLAVE instead.
#define TEST_ROLE_MASTER
// #define TEST_ROLE_SLAVE

#if defined(TEST_ROLE_MASTER) && defined(TEST_ROLE_SLAVE)
#error "Define only one of TEST_ROLE_MASTER or TEST_ROLE_SLAVE"
#elif !defined(TEST_ROLE_MASTER) && !defined(TEST_ROLE_SLAVE)
#error "Define TEST_ROLE_MASTER or TEST_ROLE_SLAVE"
#endif

constexpr uint32_t PING_INTERVAL_MS = 1000;
constexpr uint32_t RX_TIMEOUT_MS = 1000;
constexpr uint32_t SLAVE_TURNAROUND_MS = 10;

// RadioLib's Module uses the normal Arduino SPI interface. On an AVR Arduino
// board SPI.begin() maps to D13/D12/D11. Boards with a different SPI pinout
// must adapt the SPI instance/pin configuration for that board.
SX1262 radio = new Module(
  SX1262_NSS,
  SX1262_DIO1,
  SX1262_NRESET,
  SX1262_BUSY
);

struct TestStats {
  uint32_t tx = 0;
  uint32_t txErrors = 0;
  uint32_t rx = 0;
  uint32_t rxErrors = 0;
  uint32_t attempts = 0;
  uint32_t successful = 0;
  uint32_t timeouts = 0;
  uint32_t invalidPackets = 0;
  float rssiSum = 0.0f;
  float snrSum = 0.0f;
  uint32_t signalSamples = 0;
};

TestStats stats;
bool radioReady = false;

void printRadioError(const __FlashStringHelper* operation, int state) {
  Serial.print(F("[ERR] "));
  Serial.print(operation);
  Serial.print(F(" failed ("));
  Serial.print(state);
  Serial.println(F(")"));
}

void stopOnError(const __FlashStringHelper* operation, int state) {
  printRadioError(operation, state);
  Serial.println(F("[ERR] Radio test stopped; power-cycle after correcting the fault."));
  radioReady = false;
}

void recordSignal(float rssi, float snr) {
  stats.rssiSum += rssi;
  stats.snrSum += snr;
  stats.signalSamples++;
}

void printStatistics() {
  const uint32_t failed =
    (stats.attempts >= stats.successful) ? stats.attempts - stats.successful : 0;
  const float loss = (stats.attempts == 0)
    ? 0.0f
    : (100.0f * static_cast<float>(failed) / static_cast<float>(stats.attempts));

  Serial.print(F("[STAT] tx="));
  Serial.print(stats.tx);
  Serial.print(F(" txerr="));
  Serial.print(stats.txErrors);
  Serial.print(F(" rx="));
  Serial.print(stats.rx);
  Serial.print(F(" rxerr="));
  Serial.print(stats.rxErrors);
  Serial.print(F(" pass="));
  Serial.print(stats.successful);
  Serial.print(F(" timeout="));
  Serial.print(stats.timeouts);
  Serial.print(F(" invalid="));
  Serial.print(stats.invalidPackets);
  Serial.print(F(" loss="));
  Serial.print(loss, 2);
  Serial.print(F("%"));

  if (stats.signalSamples > 0) {
    Serial.print(F(" avg_rssi="));
    Serial.print(stats.rssiSum / static_cast<float>(stats.signalSamples), 1);
    Serial.print(F("dBm avg_snr="));
    Serial.print(stats.snrSum / static_cast<float>(stats.signalSamples), 1);
    Serial.print(F("dB"));
  }
  Serial.println();
}

bool parseSequence(const String& packet, const char* prefix, uint32_t& sequence) {
  if (!packet.startsWith(prefix)) {
    return false;
  }

  String number = packet.substring(strlen(prefix));
  number.trim();
  if (number.length() == 0) {
    return false;
  }

  for (uint16_t i = 0; i < number.length(); i++) {
    if (number[i] < '0' || number[i] > '9') {
      return false;
    }
  }

  char* end = nullptr;
  const unsigned long parsed = strtoul(number.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }

  sequence = static_cast<uint32_t>(parsed);
  return true;
}

int transmitPacket(const String& packet) {
  stats.tx++;
  const int state = radio.transmit(packet.c_str());
  if (state != RADIOLIB_ERR_NONE) {
    stats.txErrors++;
  }
  return state;
}

void printBoardAssumptions() {
  Serial.println(F("[BOARD] SX1262MB1DAS / E449V01A assumed"));
  Serial.println(F("[BOARD] SX1262 option: verify R18 populated and L8 unpopulated"));
  Serial.println(F("[BOARD] 32 MHz TCXO: DIO3-controlled; do not drive DIO3 from MCU"));
  Serial.println(F("[BOARD] RF switch: ANT_SW held HIGH; DIO2 controlled by SX1262"));
}

bool initializeRadio() {
  pinMode(SX1262_ANT_SW, OUTPUT);
  digitalWrite(SX1262_ANT_SW, HIGH);

  pinMode(SX1262_NSS, OUTPUT);
  digitalWrite(SX1262_NSS, HIGH);

  SPI.begin();
  Serial.println(F("[CFG] SPI ............. OK"));

  // Set this before begin() so the initialization sequence knows that DIO2
  // belongs to the automatic RF-switch function.
  int state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
    stopOnError(F("DIO2 RF switch"), state);
    return false;
  }

  // The final begin() argument selects the regulator. The schematic shows
  // the DCC_SW / L7 switching path, so keep this false (DC-DC mode).
  state = radio.begin(
    RADIO_FREQUENCY_MHZ,
    RADIO_BANDWIDTH_KHZ,
    RADIO_SPREADING_FACTOR,
    RADIO_CODING_RATE,
    RADIO_SYNC_WORD,
    RADIO_TX_POWER_DBM,
    RADIO_PREAMBLE_SYMBOLS,
    RADIO_TCXO_VOLTAGE,
    RADIO_USE_LDO
  );

  if (state != RADIOLIB_ERR_NONE) {
    stopOnError(F("SX1262 INIT / TCXO"), state);
    return false;
  }
  Serial.println(F("[CFG] SX1262 INIT ..... OK"));
  Serial.print(F("[CFG] TCXO ............ "));
  Serial.print(RADIO_TCXO_VOLTAGE, 1);
  Serial.println(F(" V via DIO3 ........ OK"));
  Serial.println(F("[CFG] RF SWITCH ....... DIO2 control / ANT_SW HIGH ... OK"));

  Serial.print(F("[CFG] "));
  Serial.print(RADIO_FREQUENCY_MHZ, 3);
  Serial.print(F(" MHz BW"));
  Serial.print(RADIO_BANDWIDTH_KHZ, 1);
  Serial.print(F(" SF"));
  Serial.print(RADIO_SPREADING_FACTOR);
  Serial.print(F(" CR4/"));
  Serial.print(RADIO_CODING_RATE);
  Serial.print(F(" TX"));
  Serial.print(RADIO_TX_POWER_DBM);
  Serial.println(F("dBm"));

  return true;
}

void runInitOnly() {
  static bool reported = false;
  if (!reported) {
    Serial.println(F("[BOOT] Initialization test complete"));
    Serial.println(F("[BOOT] Set TEST_MODE to MODE_TX_ONLY, MODE_RX_ONLY, or MODE_PING_PONG"));
    reported = true;
  }
  delay(1000);
}

void runTxOnly() {
  static uint32_t sequence = 1;
  static uint32_t nextRun = 0;
  if (static_cast<int32_t>(millis() - nextRun) < 0) {
    return;
  }
  nextRun = millis() + PING_INTERVAL_MS;

  char message[32];
  snprintf(message, sizeof(message), "TEST,TX,%lu", static_cast<unsigned long>(sequence++));
  Serial.print(F("[TX] "));
  Serial.println(message);

  const int state = transmitPacket(String(message));
  if (state != RADIOLIB_ERR_NONE) {
    printRadioError(F("TX"), state);
  }
  printStatistics();
}

void runRxOnly() {
  String message;
  const int state = radio.receive(message, RX_TIMEOUT_MS);
  if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    return;
  }
  if (state != RADIOLIB_ERR_NONE) {
    stats.rxErrors++;
    printRadioError(F("RX"), state);
    return;
  }

  stats.rx++;
  const float rssi = radio.getRSSI();
  const float snr = radio.getSNR();
  recordSignal(rssi, snr);
  Serial.print(F("[RX] "));
  Serial.print(message);
  Serial.print(F(" RSSI="));
  Serial.print(rssi, 1);
  Serial.print(F("dBm SNR="));
  Serial.print(snr, 1);
  Serial.println(F("dB"));
  printStatistics();
}

void runMaster() {
  static uint32_t sequence = 1;
  static uint32_t nextRun = 0;
  if (static_cast<int32_t>(millis() - nextRun) < 0) {
    return;
  }
  nextRun = millis() + PING_INTERVAL_MS;

  const uint32_t sentSequence = sequence++;
  String ping = String(F("PING,")) + String(sentSequence);
  stats.attempts++;

  Serial.print(F("[TX] PING seq="));
  Serial.println(sentSequence);
  int state = transmitPacket(ping);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioError(F("PING TX"), state);
    printStatistics();
    return;
  }

  String response;
  state = radio.receive(response, RX_TIMEOUT_MS);
  if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    stats.timeouts++;
    Serial.print(F("[RX] PONG seq="));
    Serial.print(sentSequence);
    Serial.println(F(" TIMEOUT"));
    printStatistics();
    return;
  }
  if (state != RADIOLIB_ERR_NONE) {
    stats.rxErrors++;
    printRadioError(F("PONG RX"), state);
    printStatistics();
    return;
  }

  stats.rx++;
  const float rssi = radio.getRSSI();
  const float snr = radio.getSNR();
  recordSignal(rssi, snr);

  uint32_t echoedSequence = 0;
  if (!parseSequence(response, "PONG,", echoedSequence)) {
    stats.invalidPackets++;
    Serial.print(F("[RX] invalid response: "));
    Serial.println(response);
    printStatistics();
    return;
  }

  Serial.print(F("[RX] PONG seq="));
  Serial.print(echoedSequence);
  Serial.print(F(" RSSI="));
  Serial.print(rssi, 1);
  Serial.print(F("dBm SNR="));
  Serial.print(snr, 1);
  Serial.print(F("dB"));

  if (echoedSequence == sentSequence) {
    stats.successful++;
    Serial.println(F(" PASS"));
  } else {
    stats.invalidPackets++;
    Serial.println(F(" UNEXPECTED"));
  }
  printStatistics();
}

void runSlave() {
  String request;
  const int state = radio.receive(request, RX_TIMEOUT_MS);
  if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    return;
  }
  if (state != RADIOLIB_ERR_NONE) {
    stats.rxErrors++;
    printRadioError(F("PING RX"), state);
    return;
  }

  stats.rx++;
  const float rssi = radio.getRSSI();
  const float snr = radio.getSNR();
  recordSignal(rssi, snr);

  uint32_t sequence = 0;
  if (!parseSequence(request, "PING,", sequence)) {
    stats.invalidPackets++;
    Serial.print(F("[RX] invalid packet: "));
    Serial.println(request);
    return;
  }

  Serial.print(F("[RX] PING seq="));
  Serial.print(sequence);
  Serial.print(F(" RSSI="));
  Serial.print(rssi, 1);
  Serial.print(F("dBm SNR="));
  Serial.print(snr, 1);
  Serial.println(F("dB"));

  delay(SLAVE_TURNAROUND_MS);
  String response = String(F("PONG,")) + String(sequence);
  const int txState = transmitPacket(response);
  if (txState != RADIOLIB_ERR_NONE) {
    printRadioError(F("PONG TX"), txState);
  } else {
    Serial.print(F("[TX] PONG seq="));
    Serial.println(sequence);
  }

  if ((stats.rx % 10) == 0) {
    printStatistics();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println(F("[BOOT] SX1262 radio test v0.1"));
  Serial.println(F("[BOOT] Board: SX1262MB1DAS / E449V01A"));

#if defined(TEST_ROLE_MASTER)
  Serial.println(F("[BOOT] Role: MASTER"));
#else
  Serial.println(F("[BOOT] Role: SLAVE"));
#endif

  printBoardAssumptions();
  radioReady = initializeRadio();
  if (!radioReady) {
    return;
  }

  Serial.println(F("[BOOT] READY"));
}

void loop() {
  if (!radioReady) {
    delay(1000);
    return;
  }

  switch (TEST_MODE) {
    case MODE_INIT_ONLY:
      runInitOnly();
      break;
    case MODE_TX_ONLY:
      runTxOnly();
      break;
    case MODE_RX_ONLY:
      runRxOnly();
      break;
    case MODE_PING_PONG:
#if defined(TEST_ROLE_MASTER)
      runMaster();
#else
      runSlave();
#endif
      break;
  }
}
