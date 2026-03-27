
const sensorConfig = {
  co2: {
    valueEl: document.getElementById("co2-value"),
    deltaEl: document.getElementById("co2-delta"),
    canvas: document.getElementById("co2-chart"),
    min: 350,
    max: 2200,
    data: Array(12).fill(null)
  },
  ambient: {
    valueEl: document.getElementById("ambient-value"),
    deltaEl: document.getElementById("ambient-delta"),
    canvas: document.getElementById("ambient-chart"),
    min: 15,
    max: 35,
    data: Array(12).fill(null)
  },
  humidity: {
    valueEl: document.getElementById("humidity-value"),
    deltaEl: document.getElementById("humidity-delta"),
    canvas: document.getElementById("humidity-chart"),
    min: 0,
    max: 100,
    data: Array(12).fill(null)
  },
  light: {
    valueEl: document.getElementById("light-value"),
    deltaEl: document.getElementById("light-delta"),
    canvas: document.getElementById("light-chart"),
    min: 0,
    max: 100,
    data: Array(12).fill(null)
  },
  probe: {
    valueEl: document.getElementById("probe-value"),
    deltaEl: document.getElementById("probe-delta"),
    canvas: document.getElementById("probe-chart"),
    min: 10,
    max: 30,
    data: Array(12).fill(null)
  },
  ph: {
    valueEl: document.getElementById("ph-value"),
    deltaEl: document.getElementById("ph-delta"),
    canvas: document.getElementById("ph-chart"),
    min: 4,
    max: 9,
    data: Array(12).fill(null)
  }
};

const co2Bar = document.getElementById("co2-bar");
const updatedAt = document.getElementById("updated-at");
const sourceStatus = document.getElementById("source-status");
const API_URL = "https://biosense-32860-default-rtdb.asia-southeast1.firebasedatabase.app/readings.json";

function formatValue(sensorKey, value) {
  if (value == null || Number.isNaN(value)) {
    return "--";
  }
  if (sensorKey === "co2") {
    return Math.round(value).toString();
  }
  return value.toFixed(1);
}

function calculateDelta(series) {
  const valid = series.filter((value) => typeof value === "number" && !Number.isNaN(value));
  if (valid.length < 2) {
    return "Waiting";
  }
  const prev = valid[valid.length - 2];
  const current = valid[valid.length - 1];
  if (prev === 0) {
    return "Live";
  }
  const delta = ((current - prev) / prev) * 100;
  const sign = delta >= 0 ? "+" : "";
  return `${sign}${delta.toFixed(1)}%`;
}

function drawChart(canvas, data, min, max) {
  const ctx = canvas.getContext("2d");
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (let i = 1; i < 4; i += 1) {
    const y = (height / 4) * i;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }

  const validEntries = data
    .map((value, index) => (typeof value === "number" && !Number.isNaN(value) ? { value, index } : null))
    .filter(Boolean);

  if (validEntries.length === 0) {
    ctx.fillStyle = "rgba(255,255,255,0.45)";
    ctx.font = "14px Consolas";
    ctx.fillText("Waiting for live sensor data...", 16, height / 2);
    return;
  }

  const points = validEntries.map(({ value, index }) => {
    const x = (width / (data.length - 1)) * index;
    const y = height - ((value - min) / (max - min)) * (height - 18) - 9;
    return { x, y };
  });

  const gradient = ctx.createLinearGradient(0, 0, width, height);
  gradient.addColorStop(0, "#ff6d4d");
  gradient.addColorStop(1, "#ffffff");

  ctx.beginPath();
  ctx.moveTo(points[0].x, height);
  points.forEach((point, index) => {
    const next = points[index + 1];
    if (!next) {
      ctx.lineTo(point.x, point.y);
      return;
    }
    const midX = (point.x + next.x) / 2;
    ctx.quadraticCurveTo(point.x, point.y, midX, (point.y + next.y) / 2);
  });
  ctx.lineTo(points[points.length - 1].x, height);
  ctx.closePath();

  const fillGradient = ctx.createLinearGradient(0, 0, 0, height);
  fillGradient.addColorStop(0, "rgba(255, 90, 54, 0.28)");
  fillGradient.addColorStop(1, "rgba(255, 90, 54, 0.02)");
  ctx.fillStyle = fillGradient;
  ctx.fill();

  ctx.beginPath();
  points.forEach((point, index) => {
    if (index === 0) {
      ctx.moveTo(point.x, point.y);
      return;
    }
    const prev = points[index - 1];
    const midX = (prev.x + point.x) / 2;
    ctx.quadraticCurveTo(midX, prev.y, point.x, point.y);
  });
  ctx.strokeStyle = gradient;
  ctx.lineWidth = 3;
  ctx.stroke();

  const lastPoint = points[points.length - 1];
  ctx.beginPath();
  ctx.arc(lastPoint.x, lastPoint.y, 5, 0, Math.PI * 2);
  ctx.fillStyle = "#ff5a36";
  ctx.fill();
  ctx.strokeStyle = "#fff";
  ctx.lineWidth = 2;
  ctx.stroke();
}

function renderSensor(sensorKey) {
  const sensor = sensorConfig[sensorKey];
  const current = sensor.data[sensor.data.length - 1];
  sensor.valueEl.textContent = formatValue(sensorKey, current);
  sensor.deltaEl.textContent = calculateDelta(sensor.data);
  drawChart(sensor.canvas, sensor.data, sensor.min, sensor.max);
}

function updateBar() {
  const co2Value = sensorConfig.co2.data[sensorConfig.co2.data.length - 1];
  if (typeof co2Value !== "number" || Number.isNaN(co2Value)) {
    co2Bar.style.width = "0%";
    return;
  }
  const normalized = ((co2Value - sensorConfig.co2.min) / (sensorConfig.co2.max - sensorConfig.co2.min)) * 100;
  co2Bar.style.width = `${Math.max(6, Math.min(100, normalized))}%`;
}

function normalizePayload(payload) {
  if (!payload || typeof payload !== "object") {
    return null;
  }

  const normalized = { ...payload };

  if (typeof normalized.humidity === "number" && normalized.humidity <= 1) {
    normalized.humidity *= 100;
  }

  if (typeof normalized.light === "number" && normalized.light <= 1) {
    normalized.light *= 100;
  }

  if (typeof normalized.light === "number") {
    normalized.light = Math.max(0, Math.min(100, normalized.light));
  }

  if (typeof normalized.probeTemp === "number" && normalized.probeTemp <= -100) {
    normalized.probeTemp = null;
  }

  if (typeof normalized.ph === "number" && (normalized.ph < 0 || normalized.ph > 14)) {
    normalized.ph = null;
  }

  return normalized;
}

function applyIncomingReading(payload) {
  if (typeof payload.co2 === "number") {
    sensorConfig.co2.data.push(payload.co2);
    sensorConfig.co2.data.shift();
  }
  if (typeof payload.ambientTemp === "number") {
    sensorConfig.ambient.data.push(payload.ambientTemp);
    sensorConfig.ambient.data.shift();
  }
  if (typeof payload.humidity === "number") {
    sensorConfig.humidity.data.push(payload.humidity);
    sensorConfig.humidity.data.shift();
  }
  if (typeof payload.light === "number") {
    sensorConfig.light.data.push(payload.light);
    sensorConfig.light.data.shift();
  }
  if (typeof payload.probeTemp === "number") {
    sensorConfig.probe.data.push(payload.probeTemp);
    sensorConfig.probe.data.shift();
  }
  if (typeof payload.ph === "number") {
    sensorConfig.ph.data.push(payload.ph);
    sensorConfig.ph.data.shift();
  }
}

async function pollApi() {
  try {
    const response = await fetch(API_URL, { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const rawPayload = await response.json();
    const payload = normalizePayload(rawPayload);
    if (!payload) {
      throw new Error("Empty Firebase payload");
    }
    applyIncomingReading(payload);
    Object.keys(sensorConfig).forEach((sensorKey) => renderSensor(sensorKey));
    updateBar();
    updatedAt.textContent = new Date(
      payload.updatedAt || payload.timestamp || Date.now()
    ).toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit"
    });
    sourceStatus.textContent = "Firebase RTDB";
  } catch (_error) {
    sourceStatus.textContent = "Waiting For Firebase";
    Object.keys(sensorConfig).forEach((sensorKey) => renderSensor(sensorKey));
    updateBar();
  }
}

window.addEventListener("resize", () => {
  Object.keys(sensorConfig).forEach((sensorKey) => renderSensor(sensorKey));
});

Object.keys(sensorConfig).forEach((sensorKey) => renderSensor(sensorKey));
setInterval(pollApi, 4000);
pollApi();
