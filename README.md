# Smart Garbage Bin — Starter Kit

ESP32 + HC-SR04 → Mosquitto (MQTT) → Node-RED → InfluxDB → Grafana, with 3 status LEDs on the ESP32.

```
smart-bin/
├── esp32/smart_bin/smart_bin.ino      # Arduino sketch
├── docker-compose.yml                  # Mosquitto, InfluxDB, Node-RED, Grafana
├── mosquitto/mosquitto.conf
├── nodered/flows.json                  # import into Node-RED
└── grafana/                            # datasource + dashboard load automatically
```

## 1. Wiring

| HC-SR04 | ESP32 |
|---|---|
| VCC | **VIN / 5V** (sensor needs 5 V) |
| GND | GND |
| TRIG | GPIO 5 |
| ECHO | **1 kΩ → GPIO 18**, and GPIO 18 → **2 kΩ → GND** |

The 1 kΩ / 2 kΩ divider drops the 5 V echo to ~3.3 V. Don't skip it — ESP32 pins are not 5 V tolerant.

| LED | ESP32 pin | Path |
|---|---|---|
| Green (0–49 %) | GPIO 25 | pin → 220 Ω → LED long leg; short leg → GND |
| Yellow (50–79 %) | GPIO 26 | same |
| Red (80–100 %) | GPIO 27 | same |

Mount the sensor in the lid's centre, pointing straight down.

## 2. Day 1 — ESP32 → MQTT → Node-RED

1. **Arduino IDE**: Boards Manager → install *esp32 by Espressif Systems*. Library Manager → install *PubSubClient*. Board = *ESP32 Dev Module*.
2. Open `smart_bin.ino` and edit the block at the top:
   - `WIFI_SSID` / `WIFI_PASSWORD` (the ESP32 only works with **2.4 GHz** Wi-Fi)
   - `MQTT_HOST` = your PC's LAN IP (`ipconfig` on Windows, `ifconfig` / `ip a` on Mac/Linux)
   - `EMPTY_DISTANCE_CM`: put the lid on the empty bin, read the distance in Serial Monitor (115200 baud), and enter that value.
3. Upload. At startup the LEDs light up one after another (self-test), then Serial Monitor prints a reading such as `{"distance":38.2,"fill":5,"status":"LOW"}` every 3 s.
4. Start the services: `docker compose up -d` (run it in the `smart-bin` folder).
5. Open Node-RED at http://localhost:1880 → ☰ menu → **Import** → paste `nodered/flows.json` → **Deploy**.
   The **Raw reading** debug node should now show live data. ✅ Day 1 done.

**Test without hardware** (works for Day 2 even if the sensor isn't ready):
```bash
docker exec mosquitto mosquitto_pub -t smartbin/bin1/data -m '{"distance":20,"fill":57,"status":"HALF"}'
```

## 3. Day 2 — InfluxDB + Grafana

The setup is already done by `docker-compose.yml`:

| Service | URL | Login |
|---|---|---|
| InfluxDB | http://localhost:8086 | admin / smartbin123 (org `smartbin`, bucket `bin_data`) |
| Grafana | http://localhost:3000 | admin / admin |

- The Node-RED flow writes to InfluxDB through its HTTP API, so you don't need to install any extra palette nodes. The second debug node shows `204` when a write succeeds.
- Check the stored data: InfluxDB → **Data Explorer** → bucket `bin_data` → measurement `bin_level` → field `fill`.
- Grafana → Dashboards → **Smart Garbage Bin** loads automatically. It has a fill gauge (green/yellow/red thresholds) and a history chart, and refreshes every 5 s.

### Flux queries (for your report or to rebuild the panels by hand)

```flux
// Current fill (gauge)
from(bucket: "bin_data")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "bin_level" and r._field == "fill")
  |> last()

// History (time series)
from(bucket: "bin_data")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "bin_level" and r._field == "fill")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

## 4. Integration test checklist

| Test | Expected fill | LED | Dashboard |
|---|---|---|---|
| Empty bin | ~0–10 % | Green | Gauge green |
| Half full (or a box at mid-height) | ~50–70 % | Yellow | Gauge yellow |
| Nearly full (hand ~8 cm below sensor) | ≥ 80 % | Red | Gauge red, history line climbs |
| Unplug the ECHO wire | — | Red blinks | No new data points |
| Stop Docker / Wi-Fi off | — | LEDs still follow the fill level | Gap in the chart |

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| Serial: `MQTT failed, rc=-2` | Wrong `MQTT_HOST`, or the PC firewall is blocking port 1883. On Windows, allow inbound TCP 1883 (or allow Docker Desktop). The PC and the ESP32 must be on the same network; campus/guest Wi-Fi often blocks device-to-device traffic, so a phone hotspot is a reliable fallback. |
| Wi-Fi never connects | Use a 2.4 GHz network and check the SSID and password (both case-sensitive). |
| Distance stays at 0 / sensor error | Check that VCC goes to 5 V and the divider is wired correctly. Try a different GPIO. |
| Fill reading jumps around | Mount the sensor straight; soft rubbish (plastic bags) reflects poorly. The sketch already takes the median of 5 readings. |
| Node-RED debug shows `401` | The token in the function node doesn't match `DOCKER_INFLUXDB_INIT_ADMIN_TOKEN`. |
| Changed InfluxDB settings but nothing changes | The init variables only run once. Reset with `docker compose down -v`, then `up -d`. |
| Grafana panel says "No data" | Set the time range to "Last 30 minutes" and confirm data exists in InfluxDB Data Explorer. |

## 6. Optional — Telegram alert (if you finish early)

In Node-RED: add a **switch** node after `mqtt in` with the rule `payload.fill >= 80`. Then add an **rbe/filter** node ("block unless value changes", on `payload.status`) so you get one alert instead of one every 3 s. Finish with an **http request** node that POSTs to
`https://api.telegram.org/bot<TOKEN>/sendMessage?chat_id=<CHAT_ID>&text=Bin%20is%20full!`

> Security note: anonymous MQTT and hard-coded tokens are fine for a lab demo, but you should say so in your report as a known limitation.
