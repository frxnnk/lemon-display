export const SCREEN_SIZE = 480;

export const CARD_TEMPLATES = {
  btc: { title: "BTC Hero", kind: "btc", source: "market:btc-usd", x: 16, y: 48, w: 448, h: 213, style: { accent: "solar" } },
  dollar: { title: "Lemon Dollar", kind: "dollar", source: "market:usdc-ars", x: 16, y: 267, w: 448, h: 213, style: { accent: "greent" } },
  stocks: { title: "Watchlist", kind: "stocks", source: "watchlist", x: 16, y: 267, w: 448, h: 213, style: { accent: "nebula" } },
  watchlist: { title: "Watchlist", kind: "watchlist", source: "watchlist", x: 16, y: 267, w: 448, h: 213, style: { accent: "nebula" } },
  chart: { title: "Chart", kind: "chart", source: "market:sparkline", x: 24, y: 286, w: 432, h: 144, style: { accent: "greent" } },
  metric: { title: "Metric", kind: "metric", source: "local-metric", x: 24, y: 286, w: 210, h: 132, style: { accent: "sky" } },
  text: { title: "Note", kind: "text", source: "local", x: 252, y: 68, w: 204, h: 132, style: { accent: "starlight" } },
  list: { title: "List", kind: "list", source: "local-list", x: 24, y: 286, w: 210, h: 144, style: { accent: "greent" } },
  sparkline: { title: "Sparkline", kind: "sparkline", source: "market:sparkline", x: 24, y: 286, w: 432, h: 144, style: { accent: "solar" } },
  weather: { title: "Weather", kind: "weather", source: "local-weather", x: 252, y: 68, w: 204, h: 132, style: { accent: "sky" } },
  custom: { title: "Custom API", kind: "custom", source: "https://api.example.com", x: 252, y: 286, w: 204, h: 132, style: { accent: "nebula" } },
};

const ACCENTS = {
  greent: "#00f068",
  solar: "#ff8700",
  nebula: "#806cf2",
  starlight: "#e7e7e7",
  sky: "#5fb3ff",
};

export function newCard(kind, index = 1) {
  const base = CARD_TEMPLATES[kind] || CARD_TEMPLATES.custom;
  const card = structuredClone(base);
  card.id = `${slug(card.title)}-${index}`;
  card.prompt = card.prompt || "";
  return card;
}

export function clampCard(card) {
  const next = { ...card };
  next.x = clampInt(next.x, 0, SCREEN_SIZE - 48);
  next.y = clampInt(next.y, 0, SCREEN_SIZE - 48);
  next.w = clampInt(next.w, 48, SCREEN_SIZE - next.x);
  next.h = clampInt(next.h, 48, SCREEN_SIZE - next.y);
  return next;
}

export function drawScreen(canvas, manifest = {}, selectedId = "") {
  canvas.width = SCREEN_SIZE;
  canvas.height = SCREEN_SIZE;
  const ctx = canvas.getContext("2d");
  const theme = manifest.theme === "light" ? "light" : "dark";
  const colors = theme === "light"
    ? { bg: "#f2f4ef", panel: "#ffffff", ink: "#111411", muted: "#5b5b5b", line: "#cfd6ca" }
    : { bg: "#030504", panel: "#07100b", ink: "#e7e7e7", muted: "#8b968d", line: "#16351f" };

  ctx.clearRect(0, 0, SCREEN_SIZE, SCREEN_SIZE);
  ctx.fillStyle = colors.bg;
  ctx.fillRect(0, 0, SCREEN_SIZE, SCREEN_SIZE);

  if (shouldDrawFirmwarePreview(manifest)) {
    drawFirmwareScreen(ctx, colors, manifest, selectedId);
    return;
  }

  drawGrid(ctx, theme);
  drawHeader(ctx, colors, manifest);
  const cards = Array.isArray(manifest.cards) ? manifest.cards : [];
  cards.forEach((card) => drawCard(ctx, clampCard(card), colors, card.id === selectedId));
  drawFooter(ctx, colors, cards.length);
}

function shouldDrawFirmwarePreview(manifest) {
  const mode = manifest.compatibility?.mode;
  return mode === "device-supported" || manifest.compatibility?.firmwareSupported === true;
}

function drawFirmwareScreen(ctx, colors, manifest, selectedId) {
  const settings = manifest.deviceSettings || {};
  const layout = settings.layout || manifest.layout || "btc_usd";
  const z2Mode = settings.z2Mode || "usd";
  const cards = Array.isArray(manifest.cards) ? manifest.cards : [];
  const btc = cards.find((card) => card.kind === "btc") || CARD_TEMPLATES.btc;
  const z2 = selectFirmwareZ2Card(cards, z2Mode);
  const zones = firmwareZones(layout);

  drawFirmwareHeader(ctx, colors, manifest, z2Mode);
  drawFirmwareBtc(ctx, { ...btc, ...zones.z1 }, colors, btc.id === selectedId);
  if (zones.z2 && z2) {
    drawCard(ctx, { ...z2, ...zones.z2 }, colors, z2.id === selectedId);
  }
  drawFirmwareFooter(ctx, colors, layout, z2Mode);
}

function selectFirmwareZ2Card(cards, z2Mode) {
  if (z2Mode === "stocks") {
    return cards.find((card) => card.kind === "stocks" || card.kind === "watchlist") || CARD_TEMPLATES.stocks;
  }
  if (z2Mode === "markets") {
    return cards.find((card) => card.kind === "chart" || card.kind === "sparkline") || {
      ...CARD_TEMPLATES.chart,
      id: "markets-z2",
      title: "Markets",
      kind: "chart",
      source: "polymarket",
    };
  }
  return cards.find((card) => card.kind === "dollar") || CARD_TEMPLATES.dollar;
}

function firmwareZones(layout) {
  if (layout === "btc_focus") {
    return { z1: { x: 16, y: 48, w: 448, h: 432 }, z2: null };
  }
  return {
    z1: { x: 16, y: 48, w: 448, h: 213 },
    z2: { x: 16, y: 267, w: 448, h: 213 },
  };
}

function drawFirmwareHeader(ctx, colors, manifest, z2Mode) {
  ctx.fillStyle = colors.ink;
  ctx.font = "700 18px Satoshi, system-ui, sans-serif";
  ctx.fillText("Lemon Box", 24, 30);
  ctx.fillStyle = colors.muted;
  ctx.font = "600 10px Satoshi, system-ui, sans-serif";
  ctx.fillText(`${(manifest.layout || "btc_usd").replace("_", " / ")} / ${z2Mode}`.toUpperCase(), 290, 29);
  ctx.fillStyle = colors.line;
  ctx.fillRect(16, 44, 448, 1);
}

function drawFirmwareBtc(ctx, card, colors, selected) {
  drawCard(ctx, card, colors, selected);
}

function drawFirmwareFooter(ctx, colors, layout, z2Mode) {
  ctx.fillStyle = colors.muted;
  ctx.font = "600 10px Satoshi, system-ui, sans-serif";
  const mode = layout === "btc_focus" ? "BTC focus" : `BTC + ${z2Mode}`;
  ctx.fillText(`${mode} / firmware preview`, 24, 468);
}

function drawHeader(ctx, colors, manifest) {
  ctx.fillStyle = colors.ink;
  ctx.font = "700 18px Satoshi, system-ui, sans-serif";
  ctx.fillText("Lemon Box", 24, 30);
  ctx.fillStyle = colors.muted;
  ctx.font = "500 11px Satoshi, system-ui, sans-serif";
  ctx.fillText((manifest.layout || "btc_usd").replace("_", " / ").toUpperCase(), 358, 29);
  ctx.strokeStyle = colors.line;
  ctx.beginPath();
  ctx.moveTo(24, 44);
  ctx.lineTo(456, 44);
  ctx.stroke();
}

function drawCard(ctx, card, colors, selected) {
  const accent = ACCENTS[card.style?.accent] || ACCENTS.greent;
  ctx.save();
  roundedRect(ctx, card.x, card.y, card.w, card.h, 16);
  ctx.fillStyle = colors.panel;
  ctx.fill();
  ctx.strokeStyle = selected ? accent : colors.line;
  ctx.lineWidth = selected ? 3 : 1;
  ctx.stroke();

  ctx.fillStyle = accent;
  ctx.fillRect(card.x + 14, card.y + 14, 28, 3);
  ctx.fillStyle = colors.ink;
  ctx.font = "700 17px Satoshi, system-ui, sans-serif";
  ctx.fillText(card.title || "Card", card.x + 14, card.y + 42, card.w - 28);
  ctx.fillStyle = colors.muted;
  ctx.font = "500 10px Satoshi, system-ui, sans-serif";
  ctx.fillText(`${card.kind || "custom"} / ${card.source || "local"}`.toUpperCase(), card.x + 14, card.y + 60, card.w - 28);

  if (card.kind === "btc") drawBtc(ctx, card, accent, colors);
  else if (card.kind === "dollar") drawDollar(ctx, card, accent, colors);
  else if (card.kind === "stocks" || card.kind === "watchlist" || card.kind === "list") drawStocks(ctx, card, accent, colors);
  else if (card.kind === "weather") drawWeather(ctx, card, accent, colors);
  else if (card.kind === "metric") drawMetric(ctx, card, accent, colors);
  else if (card.kind === "chart" || card.kind === "sparkline") drawSpark(ctx, card.x + 18, card.y + 78, card.w - 36, card.h - 98, accent);
  else drawTextBlock(ctx, card, colors);
  ctx.restore();
}

function drawBtc(ctx, card, accent, colors) {
  ctx.fillStyle = colors.ink;
  ctx.font = "800 46px Satoshi, system-ui, sans-serif";
  ctx.fillText("$109,420", card.x + 14, card.y + Math.min(card.h - 66, 118));
  ctx.fillStyle = accent;
  ctx.font = "700 16px Satoshi, system-ui, sans-serif";
  ctx.fillText("+2.4% 24H", card.x + 18, card.y + Math.min(card.h - 36, 148));
  drawSpark(ctx, card.x + 18, card.y + card.h - 56, card.w - 36, 34, accent);
}

function drawDollar(ctx, card, accent, colors) {
  ctx.fillStyle = colors.ink;
  ctx.font = "800 30px Satoshi, system-ui, sans-serif";
  ctx.fillText("$1,236.20", card.x + 14, card.y + card.h - 48);
  ctx.fillStyle = colors.muted;
  ctx.font = "600 13px Satoshi, system-ui, sans-serif";
  ctx.fillText("Bid 1232 / Ask 1240", card.x + 14, card.y + card.h - 22);
  ctx.fillStyle = accent;
  ctx.fillRect(card.x + card.w - 48, card.y + 18, 24, 24);
}

function drawStocks(ctx, card, accent, colors) {
  const rows = Array.isArray(card.preview?.rows) ? card.preview.rows : null;
  const symbols = rows ? rows.map((row) => row.symbol) : ["AAPL", "TSLA", "NVDA"];
  ctx.font = "700 14px Satoshi, system-ui, sans-serif";
  symbols.slice(0, 4).forEach((symbol, i) => {
    const y = card.y + 82 + i * 24;
    ctx.fillStyle = i === 0 ? accent : colors.ink;
    ctx.fillText(symbol, card.x + 14, y);
    ctx.fillStyle = colors.muted;
    const change = rows?.[i]?.change ?? (i === 1 ? -0.8 : 1.2);
    ctx.fillText(`${change > 0 ? "+" : ""}${change}%`, card.x + card.w - 62, y);
  });
}

function drawWeather(ctx, card, accent, colors) {
  const preview = card.preview || {};
  ctx.fillStyle = accent;
  ctx.font = "800 38px Satoshi, system-ui, sans-serif";
  ctx.fillText(`${preview.tempC ?? 22}C`, card.x + 14, card.y + Math.min(card.h - 40, 112));
  ctx.fillStyle = colors.ink;
  ctx.font = "700 16px Satoshi, system-ui, sans-serif";
  ctx.fillText(preview.city || "Local", card.x + 14, card.y + card.h - 48, card.w - 28);
  ctx.fillStyle = colors.muted;
  ctx.font = "600 12px Satoshi, system-ui, sans-serif";
  ctx.fillText(preview.condition || "preview-only", card.x + 14, card.y + card.h - 24, card.w - 28);
}

function drawMetric(ctx, card, accent, colors) {
  const preview = card.preview || {};
  ctx.fillStyle = accent;
  ctx.font = "800 34px Satoshi, system-ui, sans-serif";
  ctx.fillText(String(preview.value ?? preview.price ?? "42"), card.x + 14, card.y + Math.min(card.h - 36, 104));
  ctx.fillStyle = colors.muted;
  ctx.font = "600 12px Satoshi, system-ui, sans-serif";
  ctx.fillText("LIVE METRIC", card.x + 14, card.y + card.h - 24);
}

function drawTextBlock(ctx, card, colors) {
  if (card.preview?.value !== undefined) {
    ctx.fillStyle = colors.ink;
    ctx.font = "800 28px Satoshi, system-ui, sans-serif";
    ctx.fillText(String(card.preview.value), card.x + 14, card.y + Math.min(card.h - 42, 106), card.w - 28);
    return;
  }
  ctx.fillStyle = colors.muted;
  ctx.font = "600 14px Satoshi, system-ui, sans-serif";
  wrapText(ctx, card.prompt || "Prompt-driven surface. Export now, firmware generator later.", card.x + 14, card.y + 84, card.w - 28, 19);
}

function drawGrid(ctx, theme) {
  ctx.strokeStyle = theme === "light" ? "rgba(0,0,0,0.05)" : "rgba(0,240,104,0.055)";
  ctx.lineWidth = 1;
  for (let i = 0; i <= SCREEN_SIZE; i += 24) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i, SCREEN_SIZE);
    ctx.moveTo(0, i);
    ctx.lineTo(SCREEN_SIZE, i);
    ctx.stroke();
  }
}

function drawFooter(ctx, colors, count) {
  ctx.fillStyle = colors.muted;
  ctx.font = "600 10px Satoshi, system-ui, sans-serif";
  ctx.fillText(`${count} cards / local preset`, 24, 462);
}

function drawSpark(ctx, x, y, w, h, color) {
  ctx.strokeStyle = color;
  ctx.lineWidth = 3;
  ctx.beginPath();
  for (let i = 0; i < 28; i++) {
    const px = x + (w * i) / 27;
    const wave = Math.sin(i * 0.7) * 0.28 + Math.cos(i * 0.23) * 0.24;
    const py = y + h * (0.48 - wave);
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  }
  ctx.stroke();
}

function roundedRect(ctx, x, y, w, h, r) {
  const rr = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + rr, y);
  ctx.lineTo(x + w - rr, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + rr);
  ctx.lineTo(x + w, y + h - rr);
  ctx.quadraticCurveTo(x + w, y + h, x + w - rr, y + h);
  ctx.lineTo(x + rr, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - rr);
  ctx.lineTo(x, y + rr);
  ctx.quadraticCurveTo(x, y, x + rr, y);
  ctx.closePath();
}

function wrapText(ctx, text, x, y, maxWidth, lineHeight) {
  const words = String(text).split(/\s+/);
  let line = "";
  for (const word of words) {
    const test = line ? `${line} ${word}` : word;
    if (ctx.measureText(test).width > maxWidth && line) {
      ctx.fillText(line, x, y);
      line = word;
      y += lineHeight;
    } else {
      line = test;
    }
  }
  if (line) ctx.fillText(line, x, y);
}

function clampInt(value, min, max) {
  const parsed = Number.parseInt(value, 10);
  const n = Number.isFinite(parsed) ? parsed : min;
  return Math.max(min, Math.min(max, n));
}

function slug(value) {
  return String(value || "card").trim().toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "card";
}
