# ESP32-S3 WiFi Jammer

A single-file ESP32-S3 Arduino sketch that performs deauthentication-frame
flooding against 802.11 access points within radio range. The device scans for
nearby APs, caches their BSSIDs and channels, then continuously injects deauth
frames spoofed from each AP's real BSSID in order to force associated clients
off the network.

Built for security research, wireless robustness testing, and authorized
penetration engagements.

---

## Features

- **Targeted deauth flood.** Sends deauth frames with the real AP's BSSID in
  both the SA and BSSID fields, which is what causes client firmware to honor
  the frame. Broadcast-destination deauths disconnect every client on the BSSID,
  not just one.
- **Automatic target discovery.** Scans all 13 channels at boot and every 30
  seconds thereafter, caching BSSID + channel for up to 64 APs.
- **Channel-aware hopping.** Only dwells on channels that actually have a
  cached AP, so airtime is spent where it matters instead of sweeping the whole
  band.
- **Full power.** Sets TX power to the ESP32-S3 ceiling (20 dBm) at startup.
- **Live telemetry.** Serial status line every 5 seconds reporting successful
  transmissions, rejected transmissions, and cached AP count — so you know
  whether frames are leaving the radio and whether the driver is refusing them.
- **Fully autonomous.** No buttons, no serial commands, no target selection.
  Runs from power-on until power is removed.

---

## Hardware

- **ESP32-S3** dev board (any variant — DevKitC-1, custom PCB, etc.)
- USB cable for flashing and serial monitoring
- An external antenna is strongly recommended. The onboard PCB antenna has
  limited range; an external 2.4 GHz antenna meaningfully extends reach.

---

## Software requirements

| Component | Version used |
|---|---|
| Arduino IDE | 2.0.x or 1.8.x |
| ESP32 Arduino core | **2.0.17** (also works on 2.0.x line) |
| Toolchain | `xtensa-esp32s3-elf-gcc esp-2021r2-patch5-8.4.0` (bundled with core) |
| Libraries | none — `WiFi.h`, `esp_wifi.h` ship with the core |

Board selection in the IDE:

```
Tools → Board          → ESP32S3 Dev Module
Tools → Port           → <your COM/tty port>
Tools → Upload Speed   → 921600
Tools → Flash Size     → 4MB (or match your board)
```

---

## Installation

### 1. Required code in the sketch

The ESP-IDF WiFi driver blocks deauthentication frames at the driver layer by
default via an internal validation function. Raw deauth frames are rejected
with `ESP_ERR_NOT_SUPPORTED` and produce this in the serial log:

```
E (16641) wifi:unsupport frame type: 0c0
```

The sketch overrides that check with a function that always returns success:

```cpp
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}
```

### 2. Required linker flag

The override will not link without `-zmuldefs`, because the original symbol in
the precompiled ESP-IDF library would otherwise conflict. This requires a
one-time edit to the core's `platform.txt`.

Edit:

```
C:\Users\<you>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.17\platform.txt
```

Find the line beginning:

```
compiler.c.elf.libs.esp32s3=
```

Add `-zmuldefs` immediately after the `=`:

```
compiler.c.elf.libs.esp32s3=-zmuldefs -lesp_ringbuf ...
```

Optionally suppress the duplicate-symbol warning by adding `-w` to the
`build.extra_flags.esp32s3=` line as well.

Save, then **restart the Arduino IDE** — `platform.txt` is only read at IDE
startup.

> Note: this edit is wiped whenever the ESP32 core is updated. After a core
> update, re-apply `-zmuldefs` and re-flash.

### 3. Flash

Open `wifi_jammer.ino`, select the board and port, and upload. The serial
monitor at **115200 baud** will show the boot sequence and then status lines.

---

## Usage

1. Power the board.
2. Open the serial monitor at 115200 baud.
3. The sketch scans, prints the number of APs found, and begins flooding.

Expected output:

```
[ESP32-S3] jammer active
[*] scanning...
[*] 7 APs cached
[*] tx ok=140 fail=0 aps=7
[*] tx ok=280 fail=0 aps=7
...
```

`tx ok` should climb steadily. `fail` should stay at or near zero — if it
climbs, see Troubleshooting.

---

## How it works

**Frame structure.** Each deauth is a 26-byte 802.11 management frame:

| Field | Value | Why |
|---|---|---|
| Frame Control | `0xC0 0x00` | Deauthentication subtype |
| DA (destination) | `FF:FF:FF:FF:FF:FF` | Broadcast — disconnect all clients on the BSSID |
| SA (source) | **Target AP's real BSSID** | Spoofed, so the frame appears to originate from the AP |
| BSSID | **Target AP's real BSSID** | Must match the client's associated BSSID or the client drops the frame |
| Reason code | `0x0007` | "Class 3 frame received from nonassociated STA" — widely honored |

The critical detail is that **SA and BSSID are the real AP's MAC, not broadcast**.
A station only acts on a deauth when the BSSID matches the AP it is currently
associated with. Broadcasting the BSSID field — a common mistake in simpler
jammers — produces frames that every client silently discards.

**Channel management.** The ESP32-S3 has one radio. It can only transmit on one
channel at a time. The loop:

1. Walks channels 1–13.
2. Skips channels with no cached APs.
3. On active channels, sends a burst of deauths per cached AP.
4. Dwells 400 ms before moving on.

The dwell time is the tunable trade-off: too short and clients re-authenticate
in the gaps between visits; too long and coverage of other channels suffers.

**Rescanning.** Every 30 seconds the sketch runs a blocking scan to pick up APs
that came into range after boot. Scanning briefly interrupts transmission; that
is normal.

---

## Configuration

Tune these at the top of the sketch:

| Macro | Default | Effect |
|---|---|---|
| `DEAUTH_BURST` | `20` | Frames sent per AP per channel visit. Higher = more disruptive, more airtime. |
| `DWELL_MS` | `400` | Time spent on each active channel. |
| `SCAN_INTERVAL_MS` | `30000` | How often APs are re-discovered. |
| `MAX_APS` | `64` | Cap on cached targets. Increase if you expect a dense environment. |

---

## Troubleshooting

### `wifi:unsupport frame type: 0c0` in the serial log

The driver override is missing or `-zmuldefs` was not applied. Both steps in
[Installation](#installation) are required. Restart the IDE after editing
`platform.txt`.

### `tx ok` climbs but clients stay connected

Almost always **PMF / 802.11w**. Protected Management Frames cryptographically
authenticate deauth and disassoc frames, so forged ones are dropped by the
client regardless of content.

- **WPA3** networks mandate PMF. This technique does not work against them.
- **WPA2** networks may have PMF enabled optionally. Check the AP's wireless
  security settings for "Protected Management Frames" or "802.11w".
- Mixed networks: expect partial effect — PMF-capable clients stay connected,
  non-PMF clients drop.

This is a protocol-level limit, not a bug in the sketch.

### `tx fail` climbs

Read the return value of `esp_wifi_80211_tx()`:

| Return | Meaning |
|---|---|
| `ESP_ERR_INVALID_ARG` | Channel not set, or invalid frame |
| `ESP_ERR_WIFI_IF` | Wrong interface mode |
| `ESP_ERR_NOT_SUPPORTED` | Sanity check still active — `-zmuldefs` not applied |

### `0 APs cached`

- No networks in range (move closer / use an external antenna).
- Scan ran while the radio was mid-hop. The next 30-second refresh should
  pick them up.

### Board boots and immediately reboots

Usually a brownout on weak USB power during TX bursts. Use a powered hub or a
supply that can sustain the ESP32-S3's peak current draw.

---

## Limitations

- **WPA3 / PMF-enabled networks are immune.** No spoofed management frame will
  be honored.
- **Single radio.** One ESP32 can only disrupt one channel at a time. Wide-band
  environments require multiple devices or a device with multiple radios.
- **Range.** Limited by antenna and TX power. Deauth frames must reach the
  client, not just the AP.
- **5 GHz and 6 GHz are not covered.** ESP32-S3 is 2.4 GHz only.
- **Core-version dependent.** The `platform.txt` patch is tied to ESP32 core
  2.0.17. A different core version means a different `platform.txt` path and a
  re-applied flag.
- **Some clients resist regardless.** A handful of hardened driver stacks
  rate-limit or ignore broadcast-destination deauths even without PMF.

---

## Disclaimer

This project is developed strictly for educational purposes, security research, and authorized penetration testing. Unauthorized network access, disruption, interception, or other illegal and unethical activity is prohibited.

You are solely responsible for using this toolkit legally and obtaining permission before testing any network or device. The developer assumes no responsibility for misuse, damage, data loss, service interruption, or legal consequences resulting from unauthorized use.

### Legal note

Deliberately disrupting a wireless network you do not own or have explicit
written authorization to test is a criminal offense in most jurisdictions. In
the United States this falls under the Computer Fraud and Abuse Act
(18 U.S.C. § 1030), with additional exposure under 47 U.S.C. § 333 for
interference with licensed radio communications. Equivalent statutes exist in
the EU (Directive 2013/40/EU implementations), the UK (Computer Misuse Act
1990), Canada (Criminal Code s. 342.1), Australia (Criminal Code Act 1995
Part 10.7), and most other jurisdictions.

Deauthentication is a denial-of-service technique. It does not grant access, it
interrupts service. Testing against production networks — including your own
employer's, without written scope — can constitute a service interruption
affecting real users.

Legitimate use means one of:

- A network you own and control.
- A lab or test environment isolated from production.
- A signed engagement with explicit, written scope covering wireless
  disruption testing.

"Educational purposes" is not a legal defense for using this against a network
you were not authorized to test. Obtain written permission, keep it on file,
and stay within the defined scope.

---

## License

No license is granted. This code is provided as-is for the purposes described
in the Disclaimer. Redistribution, resale, or incorporation into commercial
products is not authorized.
