// Prints every message on the vitals topic. Use it to check the contract
// (e.g. is the Wokwi ESP32 publishing the right JSON?).
// Usage: node subscribe.js
const mqtt = require("mqtt");

const BROKER = "mqtt://broker.hivemq.com:1883";
const TOPIC = "ccesbhagya/ward1/vitals";

const client = mqtt.connect(BROKER, {
  clientId: "node-sub-" + Math.random().toString(16).slice(2, 10),
  reconnectPeriod: 3000
});

client.on("connect", () => {
  console.log("Connected to " + BROKER);
  client.subscribe(TOPIC, err => {
    if (err) console.error("Subscribe failed:", err.message);
    else console.log("Listening on " + TOPIC + " ... (Ctrl+C to stop)\n");
  });
});

client.on("message", (topic, payload) => {
  const raw = payload.toString();
  const time = new Date().toLocaleTimeString();
  let obj;
  try { obj = JSON.parse(raw); }
  catch (e) { console.log("[" + time + "] NOT VALID JSON: " + raw); return; }

  // Contract check: field names and number types
  const problems = [];
  for (const k of ["hr", "spo2", "temp", "ts"]) {
    if (typeof obj[k] !== "number") problems.push(k + " is " + (k in obj ? typeof obj[k] : "missing") + " (should be a number)");
  }
  if (!["stable", "warning", "critical"].includes(obj.status)) problems.push("status must be stable|warning|critical");

  console.log("[" + time + "] " + raw + (problems.length ? "\n    CONTRACT PROBLEM: " + problems.join("; ") : ""));
});

client.on("reconnect", () => console.log("Reconnecting..."));
client.on("error", e => console.error("MQTT error:", e.message));

process.on("SIGINT", () => {
  console.log("\nStopping.");
  client.end(true, () => process.exit(0));
});
