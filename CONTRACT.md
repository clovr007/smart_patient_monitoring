# Shared Contract: Smart Patient Health Monitoring

Both the Wokwi firmware and the dashboard must follow this exactly. Do not change any value without telling the other person.

## Broker
| Side | URL / host | Port |
|------|-----------|------|
| ESP32 (Wokwi) | `broker.hivemq.com` | 1883 (plain MQTT) |
| Dashboard (browser) | `wss://broker.hivemq.com:8884/mqtt` | 8884 (WebSocket Secure) |

The ESP32 must use a **random client ID** on every boot (e.g. `"esp32-" + random(0xffff)`), otherwise it will clash with other users of the public broker.

## Vitals topic
```
ccesbhagya/ward1/vitals
```
Lowercase, exact. One typo and nothing shows up.

## Payload (publish every 2 seconds)
```json
{"hr":78,"spo2":97,"temp":36.8,"status":"stable","ts":12345}
```
- `hr`: number, beats per minute (integer)
- `spo2`: number, percent (integer)
- `temp`: number, degrees Celsius (one decimal)
- `status`: `"stable"` | `"warning"` | `"critical"` (lowercase)
- `ts`: device `millis()`
- All numbers must be **numbers, not strings**. Field names are exactly `hr`, `spo2`, `temp`, `status`, `ts`.

## Thresholds
| Vital | Warning | Critical |
|-------|---------|----------|
| SpO2 | < 95 | < 92 |
| Heart rate | < 55 or > 110 | < 50 or > 120 |
| Temp (C) | > 37.5 | > 38.0 |

`status` = the worst of the three vitals.

## Phone alert (ntfy)
- On `critical` only, HTTP POST to `http://ntfy.sh/ccesbhagya-alerts`
- Body: short readable text, e.g. `CRITICAL: SpO2 88%, HR 131`
- 30 second cooldown between alerts

## Integration check
1. Friend runs Wokwi and shows the Serial Monitor printing the JSON.
2. Run `node tools/subscribe.js`. If messages appear, the contract is right.
3. Open the dashboard. If the subscriber works but the dashboard does not, the bug is in the dashboard.
