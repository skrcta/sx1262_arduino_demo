# SX1262 Radio Board Test Firmware — Design Specification

## 1. Purpose

Design a small Arduino firmware project to bring up and test a **Semtech SX1262MB1DAS / E449V01A** radio shield.

The firmware is intended for:

- Initial board bring-up
- SPI / reset / BUSY / interrupt verification
- LoRa TX and RX verification
- Two-board ping/pong testing
- RSSI, SNR and packet-loss measurement
- Repeatable bench or production-style functional testing

Use **RadioLib** for SX1262 control. Do not introduce LoRaWAN in the first implementation.

---

## 2. Hardware Being Tested

The supplied schematic identifies the radio board as an **SX1261/2 Evaluation Board, PCB E449V01A**, with:

- SX1262 transceiver option
- 32 MHz TCXO
- PE4259 RF switch
- DIO2 connected to the RF switch control
- DIO3 used for TCXO control
- mbed / Arduino-style shield connectors
- 3.3 V radio supply
- SMA RF output

For the **SX1262MB1DAS** variant, Semtech identifies the board as:

- SX1262
- India configuration
- Nominal 866 MHz RF matching
- TCXO
- RF switch
- Maximum SX1262 TX capability of +22 dBm

### Important RF requirement

**Do not transmit without an antenna or suitable RF load connected.**

For bring-up, start with low/moderate TX power, for example **10 dBm**. Higher output power is not required for the initial functional test.

---

## 3. Arduino / Shield Pin Mapping

The Semtech mbed shield pin mapping is:

| SX1262 / Shield Signal | Arduino Pin |
|---|---:|
| SCK | D13 |
| MISO | D12 |
| MOSI | D11 |
| NSS | D7 |
| ANT_SW | D8 |
| DIO1 | D5 |
| BUSY | D3 |
| NRESET | A0 |
| FREQ_SEL | A1 |
| DEVICE_SEL | A2 |
| XTAL_SEL | A3 |

The firmware should define these in one hardware configuration section.

Example naming:

```cpp
#define SX1262_SCK       13
#define SX1262_MISO      12
#define SX1262_MOSI      11
#define SX1262_NSS        7
#define SX1262_ANT_SW     8
#define SX1262_DIO1       5
#define SX1262_BUSY       3
#define SX1262_NRESET    A0

#define SX1262_FREQ_SEL  A1
#define SX1262_DEVICE_SEL A2
#define SX1262_XTAL_SEL  A3
```

Use the Arduino board variant's symbolic `Dxx` / `Axx` definitions where available rather than assuming raw MCU GPIO numbers.

---

## 4. RadioLib Hardware Object

The SX1262 instance requires:

```cpp
SX1262 radio = new Module(
    SX1262_NSS,
    SX1262_DIO1,
    SX1262_NRESET,
    SX1262_BUSY
);
```

SPI uses the normal Arduino SPI interface:

- SCK = D13
- MISO = D12
- MOSI = D11

The implementation should use `SPI.begin()` or the platform's normal Arduino SPI initialization unless the selected MCU core requires an explicit SPI instance.

---

## 5. RF Switch Handling

The schematic uses a **PE4259** RF switch.

Connections are:

- SX1262 `DIO2` -> PE4259 control input
- `ANT_SW` -> second PE4259 control / supply-selection input

For this board configuration:

```cpp
pinMode(SX1262_ANT_SW, OUTPUT);
digitalWrite(SX1262_ANT_SW, HIGH);
```

Set `ANT_SW` before radio operation.

Configure the SX1262 so DIO2 controls the RF switch.

RadioLib currently enables DIO2 RF-switch control during SX126x initialization, but the implementation may call:

```cpp
radio.setDio2AsRfSwitch(true);
```

explicitly if useful for clarity.

Do not manually toggle DIO2 from the MCU. DIO2 is controlled by the SX1262.

---

## 6. TCXO Handling

This board uses a **32 MHz TCXO** and the schematic connects SX1262 `DIO3` to the TCXO supply/control circuit.

The host MCU must **not drive DIO3 directly**.

RadioLib should configure the SX1262 TCXO control.

Use a starting TCXO configuration of:

```text
TCXO voltage: 1.6 V
```

RadioLib's SX1262 API supports TCXO configuration through the SX1262 initialization/configuration interface.

If initialization fails specifically around TCXO control, verify the installed RadioLib version and the exact board TCXO voltage before changing this value.

---

## 7. Initial LoRa Configuration

Use a conservative, easy-to-debug configuration:

| Parameter | Initial value |
|---|---:|
| Frequency | 866.0 MHz |
| Bandwidth | 125 kHz |
| Spreading Factor | SF9 |
| Coding Rate | 4/7 |
| Sync Word | Private / `0x12` |
| TX Power | 10 dBm |
| Preamble | 8 symbols |
| Header | Explicit |
| CRC | Enabled |
| TCXO | 1.6 V |

Both test boards must use identical modem parameters.

The first implementation should keep these values in one configuration block so they can easily be changed.

Prefer the current RadioLib `ConfigLoRa_t` API if supported by the installed library version. If the Arduino environment has an older RadioLib release, the legacy positional `begin(...)` API is acceptable.

---

## 8. Firmware Structure

Keep the firmware simple.

Suggested source structure:

```text
sx1262_test/
├── sx1262_test.ino
├── radio_config.h        # optional
└── README.md             # optional
```

A single `.ino` file is acceptable for the first implementation.

Avoid unnecessary abstractions until the radio is proven working.

---

## 9. Bring-Up Sequence

On startup:

1. Start serial at 115200 baud.
2. Print firmware name and build/version string.
3. Configure `ANT_SW` as output HIGH.
4. Initialize SPI.
5. Construct / initialize the SX1262 through RadioLib.
6. Configure TCXO.
7. Configure DIO2 RF-switch operation.
8. Configure LoRa parameters.
9. Print the result of each major operation.
10. Stop or enter an error state if radio initialization fails.

Expected successful console output should resemble:

```text
SX1262 RADIO TEST
Board: SX1262MB1DAS / E449V01A

SPI ............. OK
SX1262 INIT ..... OK
TCXO ............ OK
RF SWITCH ....... OK

Frequency ....... 866.000 MHz
Bandwidth ....... 125.0 kHz
SF .............. 9
CR .............. 4/7
TX Power ........ 10 dBm

READY
```

For failures, always print the **RadioLib return/error code**.

Example:

```text
SX1262 INIT ..... FAIL (-2)
```

Do not hide error values behind only `PASS` / `FAIL`.

---

## 10. Test Modes

Implement the firmware in stages.

### Stage 1 — Initialization Test

Only initialize the radio and report status.

This proves basic:

- Power
- MCU GPIO configuration
- SPI communication
- NSS
- NRESET
- BUSY
- TCXO control
- Basic SX1262 command interface

Do this first.

### Stage 2 — TX-Only Test

Transmit a packet periodically:

```text
TEST,TX,<sequence>
```

Example:

```text
TEST,TX,000001
TEST,TX,000002
TEST,TX,000003
```

Print transmit result and elapsed time.

### Stage 3 — RX-Only Test

Continuously receive packets.

For every valid packet print:

```text
RX seq=123 RSSI=-47.5dBm SNR=10.2dB
```

Also print receive errors other than normal timeout conditions.

### Stage 4 — Ping/Pong Test

This is the primary two-board functional test.

One board runs as **MASTER** and the other as **SLAVE**.

The role may initially be selected using one compile-time definition:

```cpp
#define TEST_ROLE_MASTER
```

or:

```cpp
#define TEST_ROLE_SLAVE
```

A runtime serial-command role selector can be added later if useful.

---

## 11. Ping/Pong Protocol

Keep the protocol human-readable during development.

### Master -> Slave

```text
PING,<sequence>
```

Example:

```text
PING,42
```

### Slave -> Master

```text
PONG,<sequence>
```

Example:

```text
PONG,42
```

The sequence number must be echoed unchanged so the master can detect:

- Missing responses
- Duplicate packets
- Out-of-order packets

Do not make the packet format unnecessarily complex in the first version.

---

## 12. Master State Machine

The MASTER should:

1. Increment the sequence counter.
2. Transmit `PING,<seq>`.
3. Switch to receive mode.
4. Wait for matching `PONG,<seq>`.
5. Record RSSI and SNR.
6. Count success or timeout.
7. Wait a short interval.
8. Repeat.

Suggested interval:

```text
1 second between tests
```

Use a finite receive timeout so a missing slave does not block forever.

---

## 13. Slave State Machine

The SLAVE should:

1. Stay in receive mode.
2. Receive `PING,<seq>`.
3. Record RSSI and SNR.
4. Validate the packet.
5. Wait a short turnaround time if required.
6. Transmit `PONG,<seq>`.
7. Return to receive mode.

The slave should not generate unsolicited packets in ping/pong mode.

---

## 14. Statistics

Maintain at least:

```text
TX packets
RX packets
Successful ping/pong exchanges
Timeouts
Invalid packets
Packet loss percentage
Last RSSI
Average RSSI
Last SNR
Average SNR
```

Packet loss:

```text
packet_loss = 100 * failed_exchanges / attempted_exchanges
```

Use sufficient integer width for long-running tests.

Statistics should be printable after each exchange or periodically.

Example:

```text
PING 00124 -> PASS  RSSI=-46.2 dBm  SNR=10.8 dB
PING 00125 -> PASS  RSSI=-46.8 dBm  SNR=10.5 dB
PING 00126 -> TIMEOUT

TX=126 RX=125 LOSS=0.79%
AVG_RSSI=-46.5 dBm AVG_SNR=10.6 dB
```

---

## 15. Error Handling

Every RadioLib operation returning a status code must be checked.

At minimum check:

- Radio initialization
- RF configuration calls
- Transmit
- Receive

Initialization/configuration failure should be treated as fatal.

Individual TX/RX failures should be counted and reported, but should normally allow the test loop to continue.

Avoid automatic MCU reboot loops during initial development because they can hide the original failure.

---

## 16. Serial Diagnostics

Serial output is part of the test interface.

Use consistent prefixes such as:

```text
[BOOT]
[CFG]
[TX]
[RX]
[STAT]
[ERR]
```

Example:

```text
[BOOT] SX1262 radio test v0.1
[CFG]  866.000 MHz BW125 SF9 CR4/7 TX10dBm
[TX]   PING seq=37
[RX]   PONG seq=37 RSSI=-42.7 SNR=11.4
[STAT] tx=37 rx=37 loss=0.00%
```

This makes the output easy for both humans and automated test scripts to parse.

---

## 17. Optional Board Identification Inputs

The Semtech shield provides:

- `FREQ_SEL` on A1
- `DEVICE_SEL` on A2
- `XTAL_SEL` on A3

Semtech documents these as board-configuration indicators:

```text
XTAL_SEL:
    0 = TCXO
    1 = XTAL

DEVICE_SEL:
    0 = SX1262
    1 = SX1261

FREQ_SEL:
    0 = 915 MHz
    1 = 868 MHz
```

Reading and displaying these pins is useful as a diagnostic enhancement.

Example:

```text
[BOARD] DEVICE_SEL=0 -> SX1262
[BOARD] XTAL_SEL=0   -> TCXO
[BOARD] FREQ_SEL=1   -> 868/866 MHz board family
```

Do not make the first radio bring-up depend on these inputs unless necessary.

---

## 18. Acceptance Criteria

### Initialization test

PASS if:

- RadioLib initializes the SX1262 without error.
- BUSY does not remain permanently asserted.
- No SPI / chip-not-found error is reported.

### Two-board functional test

PASS if:

- Both boards initialize.
- MASTER sends PING packets.
- SLAVE receives PING packets.
- SLAVE returns PONG packets.
- MASTER receives matching PONG packets.
- RSSI and SNR can be read.
- A 100-packet bench test completes with no unexplained communication failures at short range.

For production-style testing, packet-loss limits can be defined later after the normal bench RF environment is characterized.

---

## 19. First Implementation Deliverable

The CLI agent should initially produce:

```text
sx1262_test.ino
README.md
```

The Arduino sketch should:

- Build with RadioLib
- Use the pin mapping in this document
- Support MASTER and SLAVE roles
- Perform ping/pong operation
- Print sequence number, RSSI and SNR
- Track TX/RX/timeouts/packet loss
- Check and display RadioLib error codes
- Keep all radio parameters clearly grouped near the top of the source

Do not add LoRaWAN, display UI, EEPROM storage, networking, JSON, RTOS tasks, or other unrelated features in the first implementation.

---

## 20. Implementation Priorities

Implement in this order:

```text
1. Compile
2. SPI / SX1262 initialization
3. TCXO and RF-switch handling
4. Single packet TX
5. Single packet RX
6. Ping/pong
7. Statistics
8. Cleanup / documentation
```

If a later stage fails, preserve the earlier working stage rather than restructuring the whole project.

---

## 21. Source / Reference Notes

Hardware facts in this document are based on the supplied schematic:

`SX1262MB1DAS_868MHz_e449v01a_sch_layout_page_1.pdf`

Relevant public references:

- Semtech SX1262DVK1DAS product page  
  https://www.semtech.com/products/wireless-rf/lora-connect/sx1262dvk1das

- Semtech / mbed SX126xDVK1xAS shield pin mapping  
  https://os.mbed.com/components/SX126xDVK1xAS/

- RadioLib SX1262 class reference  
  https://jgromes.github.io/RadioLib/class_s_x1262.html

- RadioLib basics  
  https://github.com/jgromes/RadioLib/wiki/Basics

- PE4259 RF switch datasheet  
  https://www.psemi.com/pdf/datasheets/pe4259ds.pdf

---

## 22. Notes for the Firmware Agent

Treat the attached schematic and the hardware mapping in this document as the hardware source of truth.

If the selected Arduino target uses a non-standard SPI peripheral or different symbolic numbering, adapt the **MCU-side Arduino pin definitions only**. Do not change the SX1262 shield signal mapping.

When RadioLib API differences are encountered, adapt the source to the installed RadioLib version while preserving the radio configuration and test behavior specified here.
