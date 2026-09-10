# SX1262 Arduino radio test

This sketch targets the Semtech SX1262MB1DAS / E449V01A shield and uses
[RadioLib](https://github.com/jgromes/RadioLib). It implements staged bring-up,
TX-only, RX-only, and a two-board human-readable `PING,<sequence>` /
`PONG,<sequence>` test.

## Wiring cross-checked against the supplied schematic

The PDF labels these radio nets on the SX1262 and mbed-compatible headers; the
Arduino pin numbers below follow the supplied design note's mbed/Arduino shield
mapping:

| Signal | Arduino pin |
|---|---:|
| SCK | D13 |
| MISO | D12 |
| MOSI | D11 |
| NSS | D7 |
| ANT_SW | D8 |
| DIO1 | D5 |
| BUSY | D3 |
| NRESET | A0 |

The schematic also shows SX1262 DIO2 driving the PE4259 RF switch and DIO3
feeding the 32 MHz TCXO control/supply path. The sketch holds `ANT_SW` HIGH,
enables RadioLib DIO2 RF-switch control, and passes 1.6 V TCXO configuration to
RadioLib. DIO2 and DIO3 must not be driven directly by the host MCU.

The schematic uses `VDD_3V3` and does not show level translators. Use a 3.3 V
logic Arduino/MCU, or add appropriate level shifting before connecting a 5 V
Uno/Nano. Provide a clean 3.3 V supply with a common ground.

The PDF's frequency/device/module-selection area is implemented as resistor and
component stuffing options, not as host-readable `FREQ_SEL`, `DEVICE_SEL`, and
`XTAL_SEL` GPIO nets. The sketch therefore does not use A1/A2/A3 as runtime
strap inputs.

## Build and upload

1. Install the RadioLib library in the Arduino IDE Library Manager.
2. Open `sx1262_test.ino` as an Arduino sketch.
3. Select the board and its normal hardware SPI interface.
4. Start the serial monitor at **115200 baud**.
5. Connect an antenna or suitable RF load before transmitting.

The default role is MASTER. For a second board, comment out
`TEST_ROLE_MASTER`, enable `TEST_ROLE_SLAVE`, and upload again.

The default modem configuration is:

- 866.000 MHz
- 125 kHz bandwidth
- SF9
- CR 4/7
- private sync word `0x12`
- 10 dBm TX power
- 8-symbol preamble
- 1.6 V TCXO
- DC-DC regulator mode

Change the grouped constants near the top of the sketch if the assembled board
or test frequency requires different values. The schematic title says 868 MHz,
while the supplied design note specifies 866 MHz for the India configuration;
confirm the legal/test frequency for the actual deployment before increasing
power.

## Staged tests

Set `TEST_MODE` near the top of the sketch to one of:

- `MODE_INIT_ONLY` — SPI, reset/BUSY/NSS, TCXO, and radio initialization.
- `MODE_TX_ONLY` — emits `TEST,TX,<sequence>` once per second.
- `MODE_RX_ONLY` — receives arbitrary LoRa packets and reports RSSI/SNR.
- `MODE_PING_PONG` — master/slave link test; this is the default.

Every RadioLib operation used by the test is checked and its numeric error code
is printed. Initialization errors leave the firmware stopped rather than
rebooting, so the original fault remains visible in the serial log.
