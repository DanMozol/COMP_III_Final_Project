# Hardware Specification
### Room Audio Monitor — ESP32 + INMP441

---

## Bill of Materials

| Component              | Purpose                        | Approx. Cost |
|------------------------|--------------------------------|--------------|
| ESP32 development board | Microcontroller + WiFi        | ~$8          |
| INMP441 I2S microphone  | Digital audio capture         | ~$3          |
| USB cable + 5V supply   | Power                         | ~$0          |
| Breadboard + jumper wires | Prototyping                 | ~$2          |

**Total per sensor node: ~$11–13**

The INMP441 is a digital I2S MEMS microphone. It outputs clean 24-bit digital audio directly over 4 wires — no analog noise, no ADC required on the ESP32 side.

---

## Wiring — INMP441 to ESP32

```
INMP441 Pin → ESP32 Pin
───────────────────────
VDD         → 3.3V
GND         → GND
L/R         → GND        (ties mic to left channel)
WS          → GPIO 25    (word select / LRCLK)
SCK         → GPIO 26    (bit clock / BCLK)
SD          → GPIO 22    (serial data out)
```

> L/R tied to GND selects the left I2S channel. The firmware reads even indices from the stereo buffer to get left-channel samples only.

---

## Multi-Board Setup

Each additional ESP32 + INMP441 pair is wired identically. The only firmware change needed per board is the `DEVICE_ID` constant — see `firmware_spec.md`.

Recommended placement for room acoustic analysis:
- One near the mix position (front of room / console)
- One at the back of the room (where sound arrives last)
- One near the stage/speakers (near-field)

---

## Power

The ESP32 can be powered over USB from any 5V supply or laptop. Current draw is approximately 160–260 mA during WiFi transmission.
