#!/usr/bin/env node

/**
 * Smart Patient Monitor - Realistic Telemetry & Alert Publisher
 * Compliant with CONTRACT.md
 */

const mqtt = require('mqtt');

// Parse CLI Arguments
const args = process.argv.slice(2);
function getArg(flag, defaultValue) {
  const index = args.indexOf(flag);
  if (index !== -1 && args[index + 1]) {
    return args[index + 1];
  }
  return defaultValue;
}

const hasFlag = (flag) => args.includes(flag);

const BROKER_URL = getArg('--broker', 'mqtt://broker.emqx.io:1883');
const TOPIC_PREFIX = getArg('--topic', 'hospital/icu/bed-01');
const PATIENT_ID = getArg('--id', 'PT-9042');
const PATIENT_NAME = getArg('--patient', 'Eleanor Vance');
let intervalMs = parseInt(getArg('--interval', '2000'), 10);

let mode = 'normal';
if (hasFlag('--alert') || getArg('--mode', '') === 'alert') mode = 'alert';
if (hasFlag('--sos') || getArg('--mode', '') === 'sos') mode = 'sos';
if (hasFlag('--fluctuate') || getArg('--mode', '') === 'fluctuate') mode = 'fluctuate';

const DEVICE_ID = 'esp32-spm-sim01';

// Topics
const TOPIC_VITALS = `${TOPIC_PREFIX}/vitals`;
const TOPIC_ALERTS = `${TOPIC_PREFIX}/alerts`;
const TOPIC_STATUS = `${TOPIC_PREFIX}/status`;
const TOPIC_COMMANDS = `${TOPIC_PREFIX}/commands`;
const TOPIC_ACK = `${TOPIC_PREFIX}/commands/ack`;

// Color formatting
const C = {
  reset: '\x1b[0m',
  bold: '\x1b[1m',
  cyan: '\x1b[36m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
  magenta: '\x1b[35m',
  gray: '\x1b[90m',
};

console.log(`${C.bold}${C.cyan}╔═══════════════════════════════════════════════════════════════════╗${C.reset}`);
console.log(`${C.bold}${C.cyan}║   🏥 SMART PATIENT MONITOR - SIMULATION TELEMETRY ENGINE         ║${C.reset}`);
console.log(`${C.bold}${C.cyan}╚═══════════════════════════════════════════════════════════════════╝${C.reset}`);
console.log(`${C.gray}Broker:   ${C.reset}${BROKER_URL}`);
console.log(`${C.gray}Prefix:   ${C.reset}${TOPIC_PREFIX}`);
console.log(`${C.gray}Patient:  ${C.reset}${PATIENT_NAME} (${PATIENT_ID})`);
console.log(`${C.gray}Mode:     ${C.reset}${mode.toUpperCase()} (Switch with --mode normal|alert|sos|fluctuate)`);
console.log(`${C.gray}Interval: ${C.reset}${intervalMs} ms\n`);

// MQTT Client with Last Will and Testament (LWT)
const client = mqtt.connect(BROKER_URL, {
  clientId: `spm_publisher_${Math.random().toString(16).substring(2, 8)}`,
  clean: true,
  connectTimeout: 8000,
  will: {
    topic: TOPIC_STATUS,
    payload: JSON.stringify({
      device_id: DEVICE_ID,
      status: 'OFFLINE',
      reason: 'UNEXPECTED_DISCONNECT_OR_TIMEOUT',
      timestamp: Math.floor(Date.now() / 1000)
    }),
    qos: 1,
    retain: true
  }
});

let timer = null;
let buzzerMuted = false;
let stepCounter = 0;

// Patient Vital Baselines
let state = {
  heartRate: 72,
  spo2: 98.4,
  tempC: 36.8,
  systolic: 118,
  diastolic: 76,
  respRate: 16,
  ecgConnected: true,
  sosTriggered: false
};

if (mode === 'alert') {
  state.heartRate = 136;
  state.spo2 = 88.2;
  state.tempC = 39.1;
  state.systolic = 158;
  state.diastolic = 98;
} else if (mode === 'sos') {
  state.sosTriggered = true;
}

client.on('connect', () => {
  console.log(`${C.green}✔ Connected to MQTT Broker successfully.${C.reset}`);

  // Publish Online Status (Retained)
  const statusPayload = {
    device_id: DEVICE_ID,
    status: 'ONLINE',
    ip_address: '10.0.0.88',
    wifi_rssi: -54,
    firmware_version: '1.0.4-sim-node',
    uptime_seconds: 0,
    free_heap_bytes: 198420,
    buzzer_muted: buzzerMuted,
    telemetry_interval_ms: intervalMs
  };

  client.publish(TOPIC_STATUS, JSON.stringify(statusPayload), { qos: 1, retain: true });
  console.log(`${C.cyan}➔ Published ONLINE heartbeat to ${TOPIC_STATUS}${C.reset}`);

  // Subscribe to Bedside Commands
  client.subscribe(TOPIC_COMMANDS, { qos: 1 }, (err) => {
    if (!err) {
      console.log(`${C.cyan}➔ Listening for remote commands on ${TOPIC_COMMANDS}${C.reset}\n`);
    }
  });

  // Start Telemetry Loop
  startTelemetryLoop();
});

client.on('message', (topic, message) => {
  if (topic === TOPIC_COMMANDS) {
    try {
      const cmd = JSON.parse(message.toString());
      console.log(`\n${C.magenta}⚡ [COMMAND RECEIVED] Action: ${cmd.action} from ${cmd.sender || 'Unknown'}${C.reset}`);

      let ackStatus = 'SUCCESS';
      let ackMsg = '';

      switch (cmd.action) {
        case 'MUTE_BUZZER':
          buzzerMuted = true;
          ackMsg = 'Buzzer successfully muted';
          console.log(`${C.yellow}  └─ Hardware Buzzer state set to MUTED${C.reset}`);
          break;

        case 'UNMUTE_BUZZER':
          buzzerMuted = false;
          ackMsg = 'Buzzer unmuted';
          console.log(`${C.green}  └─ Hardware Buzzer state set to ACTIVE${C.reset}`);
          break;

        case 'SET_INTERVAL':
          if (cmd.params && cmd.params.interval_ms && cmd.params.interval_ms >= 500) {
            intervalMs = cmd.params.interval_ms;
            startTelemetryLoop();
            ackMsg = `Telemetry interval updated to ${intervalMs}ms`;
            console.log(`${C.green}  └─ Telemetry frequency updated to ${intervalMs} ms${C.reset}`);
          } else {
            ackStatus = 'ERROR';
            ackMsg = 'Invalid interval_ms (min 500ms)';
          }
          break;

        case 'TRIGGER_ALARM_TEST':
          console.log(`${C.red}  └─ Diagnostic Test Executed: Beep and LED flash!${C.reset}`);
          ackMsg = 'Self-test diagnostic completed successfully';
          break;

        case 'TRIGGER_SOS_ACK':
          state.sosTriggered = false;
          ackMsg = 'Emergency SOS acknowledged and reset';
          console.log(`${C.green}  └─ Bedside SOS latch cleared${C.reset}`);
          break;

        case 'SET_MODE':
          if (cmd.params && cmd.params.mode) {
            mode = cmd.params.mode;
            applyModeState(mode);
            ackMsg = `Patient simulation mode updated to ${mode}`;
            console.log(`${C.cyan}  └─ Simulation mode changed to ${mode.toUpperCase()}${C.reset}`);
          }
          break;

        default:
          ackStatus = 'UNKNOWN_COMMAND';
          ackMsg = `Unrecognized action: ${cmd.action}`;
      }

      // Publish ACK
      const ackPayload = {
        command_id: cmd.command_id || 'CMD-LOCAL',
        action: cmd.action,
        status: ackStatus,
        message: ackMsg,
        timestamp: Math.floor(Date.now() / 1000)
      };
      client.publish(TOPIC_ACK, JSON.stringify(ackPayload), { qos: 1 });
    } catch (e) {
      console.error(`${C.red}Failed to parse command payload: ${e.message}${C.reset}`);
    }
  }
});

function applyModeState(m) {
  if (m === 'normal') {
    state.heartRate = 72;
    state.spo2 = 98.4;
    state.tempC = 36.8;
    state.systolic = 118;
    state.diastolic = 76;
    state.sosTriggered = false;
  } else if (m === 'alert') {
    state.heartRate = 134;
    state.spo2 = 88.0;
    state.tempC = 39.2;
    state.systolic = 160;
    state.diastolic = 100;
  } else if (m === 'sos') {
    state.sosTriggered = true;
  }
}

function updateBiometrics() {
  stepCounter++;

  if (mode === 'normal') {
    // Slight physiological sinus fluctuation
    state.heartRate = Math.round(72 + Math.sin(stepCounter * 0.2) * 4 + (Math.random() * 2 - 1));
    state.spo2 = Number((98.5 + (Math.random() * 0.8 - 0.4)).toFixed(1));
    state.tempC = Number((36.8 + (Math.random() * 0.2 - 0.1)).toFixed(1));
    state.systolic = Math.round(118 + Math.sin(stepCounter * 0.15) * 3);
    state.diastolic = Math.round(76 + Math.sin(stepCounter * 0.15) * 2);
  } else if (mode === 'alert') {
    // Elevated tachycardia, hypoxia, high fever
    state.heartRate = Math.round(135 + Math.sin(stepCounter * 0.3) * 6 + (Math.random() * 3 - 1.5));
    state.spo2 = Number((88.0 - Math.random() * 1.5).toFixed(1));
    state.tempC = Number((39.2 + (Math.random() * 0.3 - 0.1)).toFixed(1));
    state.systolic = Math.round(158 + (Math.random() * 4 - 2));
    state.diastolic = Math.round(98 + (Math.random() * 3 - 1));
  } else if (mode === 'fluctuate') {
    // Drifts between normal and warning every 10 cycles
    const phase = Math.sin(stepCounter * 0.15);
    state.heartRate = Math.round(80 + phase * 45); // 35 to 125
    state.spo2 = Number((94 + phase * 5).toFixed(1)); // 89 to 99
    state.tempC = Number((37.2 + phase * 1.5).toFixed(1)); // 35.7 to 38.7
    state.systolic = Math.round(120 + phase * 30);
    state.diastolic = Math.round(80 + phase * 20);
  }

  // Ensure physiological bounds
  state.spo2 = Math.min(100, Math.max(70, state.spo2));
  state.heartRate = Math.min(220, Math.max(35, state.heartRate));
}

function evaluateStatus() {
  if (state.sosTriggered) return 'CRITICAL';
  if (state.heartRate > 120 || state.heartRate < 50) return 'CRITICAL';
  if (state.spo2 < 90.0) return 'CRITICAL';
  if (state.tempC >= 38.5 || state.tempC < 35.0) return 'CRITICAL';

  if (state.heartRate > 100 || state.heartRate < 60) return 'WARNING';
  if (state.spo2 < 95.0) return 'WARNING';
  if (state.tempC >= 37.6) return 'WARNING';

  return 'NORMAL';
}

function checkAndSendAlerts(triageStatus) {
  const ts = Math.floor(Date.now() / 1000);

  if (state.sosTriggered) {
    const sosAlert = {
      alert_id: `ALT-SOS-${ts}`,
      device_id: DEVICE_ID,
      patient_id: PATIENT_ID,
      timestamp: ts,
      alert_type: 'SOS_PANIC_BUTTON',
      severity: 'CRITICAL',
      message: '🚨 EMERGENCY: Bedside patient nurse call / SOS panic button triggered!',
      metric: 'sos',
      value: 1,
      unit: 'boolean',
      threshold: 0,
      acknowledged: false
    };
    client.publish(TOPIC_ALERTS, JSON.stringify(sosAlert), { qos: 1 });
    console.log(`${C.bold}${C.red}  🚨 [ALERT PUBLISHED] ${sosAlert.message}${C.reset}`);
  }

  if (state.spo2 < 90.0) {
    const hypoxiaAlert = {
      alert_id: `ALT-SPO2-${ts}`,
      device_id: DEVICE_ID,
      patient_id: PATIENT_ID,
      timestamp: ts,
      alert_type: 'CRITICAL_HYPOXIA',
      severity: 'CRITICAL',
      message: `Arterial SpO2 critically low at ${state.spo2}% (< 90%)`,
      metric: 'spo2',
      value: state.spo2,
      unit: '%',
      threshold: 90.0,
      acknowledged: false
    };
    client.publish(TOPIC_ALERTS, JSON.stringify(hypoxiaAlert), { qos: 1 });
    console.log(`${C.bold}${C.red}  🚨 [ALERT PUBLISHED] ${hypoxiaAlert.message}${C.reset}`);
  }

  if (state.heartRate > 120) {
    const tachAlert = {
      alert_id: `ALT-HR-${ts}`,
      device_id: DEVICE_ID,
      patient_id: PATIENT_ID,
      timestamp: ts,
      alert_type: 'CRITICAL_TACHYCARDIA',
      severity: 'CRITICAL',
      message: `Tachycardia detected: Heart rate ${state.heartRate} BPM (> 120 BPM)`,
      metric: 'heart_rate',
      value: state.heartRate,
      unit: 'BPM',
      threshold: 120,
      acknowledged: false
    };
    client.publish(TOPIC_ALERTS, JSON.stringify(tachAlert), { qos: 1 });
    console.log(`${C.bold}${C.red}  🚨 [ALERT PUBLISHED] ${tachAlert.message}${C.reset}`);
  }
}

function publishTelemetry() {
  updateBiometrics();
  const triageStatus = evaluateStatus();

  const tempF = Number((state.tempC * 9 / 5 + 32).toFixed(2));
  const payload = {
    device_id: DEVICE_ID,
    patient_id: PATIENT_ID,
    patient_name: PATIENT_NAME,
    timestamp: Math.floor(Date.now() / 1000),
    vitals: {
      heart_rate: state.heartRate,
      spo2: state.spo2,
      temperature_c: state.tempC,
      temperature_f: tempF,
      blood_pressure: {
        systolic: state.systolic,
        diastolic: state.diastolic
      },
      respiration_rate: state.respRate,
      ecg_lead_connected: state.ecgConnected
    },
    status: triageStatus
  };

  client.publish(TOPIC_VITALS, JSON.stringify(payload), { qos: 0 });

  // Terminal logging
  let statusBadge = `${C.green}[NORMAL]${C.reset}`;
  if (triageStatus === 'WARNING') statusBadge = `${C.yellow}[WARNING]${C.reset}`;
  if (triageStatus === 'CRITICAL') statusBadge = `${C.bold}${C.red}[CRITICAL]${C.reset}`;

  const timeStr = new Date().toLocaleTimeString();
  console.log(
    `${C.gray}${timeStr}${C.reset} ${statusBadge} ` +
    `HR: ${C.bold}${state.heartRate} bpm${C.reset} | ` +
    `SpO2: ${C.bold}${state.spo2}%${C.reset} | ` +
    `Temp: ${state.tempC}°C (${tempF}°F) | ` +
    `BP: ${state.systolic}/${state.diastolic} mmHg`
  );

  if (triageStatus === 'CRITICAL') {
    checkAndSendAlerts(triageStatus);
  }
}

function startTelemetryLoop() {
  if (timer) clearInterval(timer);
  timer = setInterval(publishTelemetry, intervalMs);
}

// Graceful exit
process.on('SIGINT', () => {
  console.log(`\n${C.yellow}Shutting down telemetry engine...${C.reset}`);
  if (timer) clearInterval(timer);

  const offlinePayload = {
    device_id: DEVICE_ID,
    status: 'OFFLINE',
    reason: 'NORMAL_SHUTDOWN',
    timestamp: Math.floor(Date.now() / 1000)
  };

  client.publish(TOPIC_STATUS, JSON.stringify(offlinePayload), { qos: 1, retain: true }, () => {
    client.end(false, () => {
      console.log(`${C.green}Disconnected cleanly. Goodbye!${C.reset}`);
      process.exit(0);
    });
  });
});

