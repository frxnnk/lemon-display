import { CARD_TEMPLATES, clampCard, drawScreen, newCard } from "./screen_renderer.js?v=studio-web-6";
import { createWorkbench3D } from "./workbench3d.js?v=studio-web-6";

const API = {
  status: "/api/status",
  capabilities: "/api/capabilities",
  sources: "/api/sources",
  sourcePreview: "/api/sources/preview",
  experiences: "/api/experiences",
  manifest: "/api/manifest",
  keys: "/api/keys",
  jobs: "/api/jobs",
  release: "/api/release",
  deployDraft: "/api/jobs/deploy-draft",
  agentDraft: "/api/jobs/agent-draft",
  devices: "/api/devices",
  devicePair: "/api/device/pair",
  deviceHealth: "/api/device/health",
  deviceConnect: "/api/device/connect",
  deviceSettings: "/api/device/settings",
  syncSettings: "/api/jobs/sync-settings",
  otaUpload: "/api/jobs/ota-upload",
  otaLatest: "/api/jobs/ota-latest",
  otaVerify: "/api/jobs/ota-verify",
  flash: "/api/jobs/flash",
};

const STUDIO_MODE = new URLSearchParams(location.search).get("mode") || "technical";
const REVIEW_MODE = STUDIO_MODE === "concept" || STUDIO_MODE === "review";
const EMBED_MODE = STUDIO_MODE === "embed";
const MARKET_DESK_VISUAL = REVIEW_MODE || EMBED_MODE;
const DEMO_SCREEN_LABELS = {
  home: "Home ambiental",
  tape: "Market Tape",
  context: "Contexto editorial",
};
const STATIC_STUDIO = location.pathname.includes("/studio/") || !["127.0.0.1", "localhost"].includes(location.hostname);
const WEB_DEVICE_KEY = "lemon-studio-web-device";
const WEB_DEVICE_TOKEN_PREFIX = "lemon-studio-web-device-token:";
const STOCK_OPTIONS = [
  { symbol: "AAPL", name: "Apple" },
  { symbol: "TSLA", name: "Tesla" },
  { symbol: "NVDA", name: "Nvidia" },
  { symbol: "SPY", name: "S&P 500" },
  { symbol: "MSTR", name: "MicroStrategy" },
  { symbol: "MELI", name: "MercadoLibre" },
  { symbol: "GGAL", name: "Galicia" },
  { symbol: "YPF", name: "YPF" },
  { symbol: "AMD", name: "AMD" },
  { symbol: "MSFT", name: "Microsoft" },
  { symbol: "GOOGL", name: "Alphabet" },
  { symbol: "AMZN", name: "Amazon" },
  { symbol: "COIN", name: "Coinbase" },
  { symbol: "QQQ", name: "Nasdaq 100" },
  { symbol: "GLD", name: "Gold ETF" },
  { symbol: "BTC-USD", name: "Bitcoin" },
];

const state = {
  status: null,
  capabilities: null,
  sources: { catalog: [], configured: [] },
  experiences: [],
  manifest: null,
  release: null,
  keys: [],
  jobs: [],
  devices: [],
  deviceHealth: null,
  lastSyncAt: "",
  sourcePreviews: {},
  selectedId: "",
  selectedSourceId: "",
  activePanel: "device",
  advanced: false,
  flashMode: "normal",
  flashArm: "",
  otaArm: "",
  pairing: false,
  demoScreen: "home",
  screenCanvas: document.createElement("canvas"),
  workbench: null,
};

function boot() {
  document.body.dataset.staticStudio = STATIC_STUDIO ? "true" : "false";
  document.body.dataset.reviewMode = REVIEW_MODE ? "true" : "false";
  document.body.dataset.embedMode = EMBED_MODE ? "true" : "false";
  if (REVIEW_MODE) {
    document.title = "Lemon Market Desk Studio · demo";
    const brandTitle = document.querySelector(".brand b");
    const brandCaption = document.querySelector(".brand span");
    if (brandTitle) brandTitle.textContent = "Lemon Market Desk Studio";
    if (brandCaption) brandCaption.textContent = "demo · no productivo";
  }
  if (EMBED_MODE) document.title = "Lemon Box · visor 3D";
  if (MARKET_DESK_VISUAL) document.fonts?.ready.then(() => renderScreen());
  state.screenCanvas.width = 480;
  state.screenCanvas.height = 480;
  if (MARKET_DESK_VISUAL) drawMarketDeskScreen(state.screenCanvas, state.demoScreen);
  else drawScreen(state.screenCanvas, fallbackManifest(), "");
  state.workbench = createWorkbench3D(byId("device-viewer"), state.screenCanvas, {
    initialView: EMBED_MODE || REVIEW_MODE ? "overview" : "front",
    onReady: () => setText("scene-state", "3D listo"),
    onError: (err) => setText("scene-state", err.message || String(err)),
  });
  bindEvents();
  deviceFromQuery();
  refreshAll();
  refreshJobs();
  setInterval(refreshJobs, 1600);
}

async function api(path, options = {}) {
  if (STATIC_STUDIO) return staticApi(path, options);
  try {
    const res = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
      body: options.body ? JSON.stringify(options.body) : undefined,
    });
    const text = await res.text();
    const payload = text ? JSON.parse(text) : {};
    if (!res.ok) throw new Error(payload.error || `HTTP ${res.status}`);
    return payload;
  } catch (err) { throw err; }
}

async function refreshAll() {
  setText("server-state", "Sincronizando Studio local");
  try {
    const [status, manifest, keys, capabilities, sources, experiences, devices] = await Promise.all([
      api(API.status),
      api(API.manifest),
      api(API.keys),
      api(API.capabilities),
      api(API.sources),
      api(API.experiences),
      api(API.devices),
    ]);
    state.status = status;
    state.manifest = ensureExperience(manifest);
    state.selectedId = cards().find((card) => card.id === state.selectedId)?.id || cards()[0]?.id || "";
    state.keys = keys.keys || [];
    state.capabilities = capabilities;
    state.sources = sources;
    state.experiences = experiences.experiences || [];
    state.devices = devices.devices || [];
    renderAll();
    setText("server-state", STATIC_STUDIO ? "Studio web preview" : "Servidor local online");
    if (state.devices.length) refreshDeviceHealth({ silent: true }).catch(() => {});
  } catch (err) {
    setText("server-state", err.message);
    state.manifest = state.manifest || fallbackManifest();
    renderAll();
  }
}

async function refreshJobs() {
  try {
    const payload = await api(API.jobs);
    state.jobs = payload.jobs || [];
    renderJobs();
  } catch (err) {
    setText("jobs-list", err.message);
  }
}

function renderAll() {
  renderNav();
  renderStatus();
  renderManifestForm();
  renderStockPicker();
  renderPalette();
  renderSourceCatalog();
  renderSourceEditor();
  renderEditor();
  renderInspector();
  renderDeviceReadiness();
  renderExperiences();
  renderKeys();
  renderJobs();
  renderScreen();
}

function renderScreen() {
  if (MARKET_DESK_VISUAL) {
    drawMarketDeskScreen(state.screenCanvas, state.demoScreen);
    const label = DEMO_SCREEN_LABELS[state.demoScreen] || DEMO_SCREEN_LABELS.home;
    setText("preview-mode", label);
    setText("preview-theme", "Demo · datos ilustrativos");
    setText("demo-screen-name", label);
    document.querySelectorAll("[data-demo-screen]").forEach((button) => {
      button.classList.toggle("active", button.dataset.demoScreen === state.demoScreen);
    });
    state.workbench?.updateTexture();
    return;
  }
  const manifest = state.manifest || fallbackManifest();
  const enriched = {
    ...manifest,
    cards: cards().map((card) => ({
      ...card,
      preview: state.sourcePreviews[card.source]?.sample,
    })),
  };
  drawScreen(state.screenCanvas, enriched, state.selectedId);
  state.workbench?.updateTexture();
}

function drawMarketDeskScreen(canvas, mode = "home") {
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#121212";
  ctx.fillRect(0, 0, 480, 480);

  ctx.fillStyle = "#00DF1A";
  ctx.fillRect(28, 26, 72, 5);
  ctx.font = "700 17px Satoshi, sans-serif";
  ctx.fillText("LEMON MARKET DESK", 28, 61);
  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "700 12px Satoshi, sans-serif";
  const subtitles = {
    home: "MERCADO ABIERTO · DATOS ILUSTRATIVOS",
    tape: "MARKET TAPE · DATOS ILUSTRATIVOS",
    context: "CONTEXTO EDITORIAL · DEMO",
  };
  ctx.fillText(subtitles[mode] || subtitles.home, 28, 86);

  if (mode === "tape") {
    drawMarketTapeDemo(ctx);
    return;
  }
  if (mode === "context") {
    drawMarketContextDemo(ctx);
    return;
  }

  ctx.fillStyle = "#E7E7E7";
  ctx.font = "700 74px Satoshi, sans-serif";
  ctx.fillText("09:42", 26, 188);
  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "400 16px Satoshi, sans-serif";
  ctx.fillText("BUENOS AIRES", 30, 216);

  ctx.strokeStyle = "rgba(237, 237, 237, 0.18)";
  ctx.beginPath();
  ctx.moveTo(28, 248);
  ctx.lineTo(452, 248);
  ctx.stroke();

  ctx.fillStyle = "#E7E7E7";
  ctx.font = "700 20px Satoshi, sans-serif";
  ctx.fillText("BITCOIN", 28, 288);
  ctx.font = "700 49px Satoshi, sans-serif";
  ctx.fillText("$118.420", 28, 345);
  ctx.fillStyle = "#00DF1A";
  ctx.font = "700 18px Satoshi, sans-serif";
  ctx.fillText("+2,4% · 24H", 30, 378);

  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "700 12px Satoshi, sans-serif";
  ctx.fillText("PRÓXIMO · CIERRE DE MERCADO 17:00", 28, 438);
  ctx.fillStyle = "#00DF1A";
  ctx.fillRect(28, 452, 424, 3);
}

function drawMarketTapeDemo(ctx) {
  ctx.fillStyle = "#E7E7E7";
  ctx.font = "700 58px Satoshi, sans-serif";
  ctx.fillText("10:30", 27, 158);
  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "400 15px Satoshi, sans-serif";
  ctx.fillText("APERTURA DE NUEVA YORK", 30, 184);

  const rows = [
    ["S&P 500", "5.842", "+0,7%"],
    ["NASDAQ", "18.416", "+1,1%"],
    ["MERVAL", "1.946K", "-0,3%"],
  ];
  rows.forEach((row, index) => {
    const y = 236 + index * 58;
    ctx.strokeStyle = "rgba(231, 231, 233, 0.16)";
    ctx.beginPath();
    ctx.moveTo(28, y - 25);
    ctx.lineTo(452, y - 25);
    ctx.stroke();
    ctx.fillStyle = "#E7E7E7";
    ctx.font = "700 18px Satoshi, sans-serif";
    ctx.fillText(row[0], 28, y);
    ctx.textAlign = "right";
    ctx.fillText(row[1], 366, y);
    ctx.fillStyle = row[2].startsWith("+") ? "#00DF1A" : "rgba(231, 231, 233, 0.62)";
    ctx.fillText(row[2], 452, y);
    ctx.textAlign = "left";
  });

  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "700 12px Satoshi, sans-serif";
  ctx.fillText("PACK TECH · MOVIMIENTO DEL DÍA", 28, 430);
  ctx.fillStyle = "#00DF1A";
  ctx.fillRect(28, 452, 424, 3);
}

function drawMarketContextDemo(ctx) {
  ctx.fillStyle = "rgba(231, 231, 233, 0.62)";
  ctx.font = "700 14px Satoshi, sans-serif";
  ctx.fillText("QUÉ PASÓ", 28, 137);
  ctx.fillStyle = "#E7E7E7";
  ctx.font = "700 45px Satoshi, sans-serif";
  ctx.fillText("BITCOIN", 27, 192);
  ctx.fillText("SUBE 2,4%", 27, 241);
  ctx.fillStyle = "#00DF1A";
  ctx.fillRect(28, 262, 118, 4);

  ctx.fillStyle = "rgba(231, 231, 233, 0.72)";
  ctx.font = "500 17px Satoshi, sans-serif";
  ctx.fillText("El movimiento acelera mientras", 28, 310);
  ctx.fillText("abre el mercado de EE.UU.", 28, 336);

  ctx.strokeStyle = "rgba(231, 231, 233, 0.2)";
  ctx.strokeRect(28, 382, 424, 52);
  ctx.fillStyle = "#E7E7E7";
  ctx.font = "700 14px Satoshi, sans-serif";
  ctx.fillText("CONTINUAR EN LEMON", 45, 414);
  ctx.fillStyle = "#00DF1A";
  ctx.fillRect(416, 397, 18, 18);
}

function renderNav() {
  document.body.dataset.activePanel = state.activePanel;
  document.body.dataset.advanced = state.advanced ? "true" : "false";
  document.body.dataset.pairing = state.pairing ? "true" : "false";
  document.querySelectorAll(".nav-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.panel === state.activePanel);
  });
  const advancedToggle = byId("advanced-toggle");
  advancedToggle.textContent = state.advanced ? "Modo simple" : "Modo tecnico";
  advancedToggle.setAttribute("aria-pressed", state.advanced ? "true" : "false");
  document.querySelectorAll("[data-section]").forEach((node) => {
    const isActive = node.dataset.section === state.activePanel;
    node.classList.toggle("section-focus", isActive);
    node.classList.toggle("section-hidden", !isActive);
  });
}

function renderStatus() {
  const s = state.status || {};
  const ports = s.ports || [];
  const active = activeDevice();
  const portSelect = byId("port-select");
  const current = portSelect.value;
  portSelect.replaceChildren();
  if (!ports.length) portSelect.append(new Option("No USB detectado", ""));
  ports.forEach((port) => portSelect.append(new Option(`${port.device} - ${port.description}`, port.device)));
  if (current) portSelect.value = current;
  setText("device-badge", deviceBadgeText(ports, active));
  setText("app-version", s.appVersion || "--");
  setText("local-build", s.localBuild?.ready ? `${Math.round(s.localBuild.size / 1024)} KB` : "missing");
  setText("recovery-ready", s.recoveryAssets?.ready ? "ready" : "missing");
  setText("latest-release", state.release ? `v${state.release.version}` : "--");
}

function renderManifestForm() {
  const m = state.manifest || fallbackManifest();
  const device = m.deviceSettings || {};
  setValue("manifest-name", m.name || "");
  setValue("manifest-theme", m.theme || "dark");
  setValue("manifest-layout", m.layout || "btc_usd");
  setValue("manifest-watchlist", (m.watchlist || []).join(", "));
  setValue("manifest-notes", m.releaseNotes || "");
  setValue("device-brightness", device.brightness ?? 255);
  setValue("device-z2", device.z2Mode || "usd");
  byId("device-pro-mode").checked = !!device.proMode;
  setText("preview-mode", m.layout === "btc_focus" ? "BTC focus" : "BTC + USD");
  setText("preview-theme", m.theme === "light" ? "Light" : "Dark");
  setText("manifest-state", `${cards().length} cards / ${sources().length} sources`);
}

function renderStockPicker() {
  const host = byId("stock-picker");
  if (!host) return;
  const selected = new Set(parseList(valueOf("manifest-watchlist")).map((item) => item.toUpperCase()));
  host.replaceChildren();
  STOCK_OPTIONS.forEach((stock) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "stock-chip";
    button.classList.toggle("selected", selected.has(stock.symbol));
    button.innerHTML = `<strong>${stock.symbol}</strong><span>${stock.name}</span>`;
    button.addEventListener("click", () => toggleStock(stock.symbol));
    host.append(button);
  });
}

function toggleStock(symbol) {
  const selected = parseList(valueOf("manifest-watchlist")).map((item) => item.toUpperCase());
  const exists = selected.includes(symbol);
  const next = exists ? selected.filter((item) => item !== symbol) : [...selected, symbol].slice(0, 8);
  setValue("manifest-watchlist", next.join(", "));
  syncWatchlistSource(next);
  state.manifest = ensureExperience(readManifestForm());
  renderManifestForm();
  renderStockPicker();
  renderDeviceReadiness();
  renderScreen();
}

function syncWatchlistSource(symbols = parseList(valueOf("manifest-watchlist"))) {
  if (!state.manifest) state.manifest = fallbackManifest();
  const nextSymbols = symbols.map((item) => item.toUpperCase()).slice(0, 8);
  let source = sources().find((item) => item.id === "watchlist" || item.kind === "market.stocks");
  if (!source) {
    source = { id: "watchlist", title: "Watchlist", kind: "market.stocks", config: { symbols: [] }, keyNames: [] };
    state.manifest.sources = [...sources(), source];
  }
  source.config = { ...(source.config || {}), symbols: nextSymbols };
  state.manifest.watchlist = nextSymbols;
}

function renderPalette() {
  const host = byId("card-palette");
  host.replaceChildren();
  visibleCardKinds().forEach((kind) => {
    const button = document.createElement("button");
    button.className = "chip";
    button.type = "button";
    button.textContent = CARD_TEMPLATES[kind].title;
    button.addEventListener("click", () => addCard(kind));
    host.append(button);
  });
}

function renderSourceCatalog() {
  const host = byId("source-catalog");
  host.replaceChildren();
  const configured = sources();
  if (!configured.length) host.append(emptyRow("No sources configured"));
  configured.forEach((source) => host.append(sourceRow(source, true)));
  (state.sources.catalog || [])
    .filter((item) => state.advanced || item.firmwareSupported)
    .forEach((item) => host.append(catalogRow(item)));
}

function sourceRow(source, configured) {
  const row = document.createElement("div");
  const preview = state.sourcePreviews[source.id];
  const health = preview ? (preview.ok ? "ok" : "needs-attention") : "idle";
  const previewMeta = sourcePreviewMeta(preview);
  row.className = `source-item ${health}`;
  const label = document.createElement("div");
  const title = document.createElement("strong");
  const meta = document.createElement("span");
  title.textContent = source.title || source.id;
  meta.textContent = configured
    ? `${sourceKindLabel(source.kind)} / ${sourceHealthLabel(preview, health)}${previewMeta}${source.keyNames?.length ? " / keys" : ""}`
    : sourceKindLabel(source.kind);
  label.append(title, meta);
  const button = document.createElement("button");
  button.className = "ghost-button";
  button.type = "button";
  button.textContent = configured ? "Usar" : "Agregar";
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    configured ? applySourceToSelected(source.id) : addSource(source.kind);
  });
  row.append(label, button);
  row.addEventListener("click", () => {
    state.selectedSourceId = source.id;
    setText("source-preview-state", source.id);
    renderSourceEditor();
  });
  return row;
}

function renderSourceEditor() {
  const source = selectedSource() || sources()[0];
  if (source && !state.selectedSourceId) state.selectedSourceId = source.id;
  setText("source-editor-state", source ? source.title || source.id : "Sin fuente");
  document.querySelectorAll("[data-source-editor]").forEach((group) => {
    group.hidden = !source || sourceEditorKind(source.kind) !== group.dataset.sourceEditor;
  });
  if (!source) return;
  const config = source.config || {};
  setValue("source-symbol", config.symbol || "BTC");
  setValue("source-currency", config.currency || "USD");
  setValue("source-symbols", (config.symbols || state.manifest?.watchlist || []).join(", "));
  setValue("source-city", config.city || config.label || "Buenos Aires");
  setValue("source-url", config.url || "");
  setValue("source-json-path", config.jsonPath || "");
}

function catalogRow(item) {
  return sourceRow({
    id: `catalog-${item.kind}`,
    title: item.title,
    kind: item.kind,
    keyNames: item.requiredKeys || [],
  }, false);
}

function emptyRow(text) {
  const row = document.createElement("div");
  row.className = "source-item";
  row.textContent = text;
  return row;
}

function renderEditor() {
  const editor = byId("screen-editor");
  editor.replaceChildren();
  editor.dataset.preview = isFirmwarePreview() ? "firmware" : "free";
  editorCards().forEach((card) => {
    const node = document.createElement("button");
    node.className = `screen-card ${card.id === state.selectedId ? "selected" : ""}`;
    node.type = "button";
    node.dataset.kind = card.kind;
    applyCardNode(node, card);
    node.innerHTML = `<strong></strong><span></span><i aria-hidden="true"></i>`;
    node.querySelector("strong").textContent = card.title || "Card";
    node.querySelector("span").textContent = `${card.kind || "custom"} / ${card.source || "local"}`;
    node.addEventListener("click", () => selectCard(card.id));
    if (!isFirmwarePreview()) {
      node.addEventListener("pointerdown", (event) => beginCardPointer(event, card.id, false, node));
      node.querySelector("i").addEventListener("pointerdown", (event) => beginCardPointer(event, card.id, true, node));
    }
    editor.append(node);
  });
}

function editorCards() {
  if (!isFirmwarePreview()) return cards();
  const all = cards();
  const btc = all.find((card) => card.kind === "btc");
  const z2Mode = state.manifest?.deviceSettings?.z2Mode || "usd";
  const z2 = firmwareZ2Card(all, z2Mode);
  const zones = firmwareZones(state.manifest?.deviceSettings?.layout || state.manifest?.layout || "btc_usd");
  return [
    btc ? { ...btc, ...zones.z1 } : null,
    zones.z2 && z2 ? { ...z2, ...zones.z2 } : null,
  ].filter(Boolean);
}

function isFirmwarePreview() {
  const compatibility = state.manifest?.compatibility || {};
  return compatibility.mode === "device-supported" || compatibility.firmwareSupported === true;
}

function firmwareZ2Card(all, z2Mode) {
  if (z2Mode === "stocks") return all.find((card) => card.kind === "stocks" || card.kind === "watchlist");
  if (z2Mode === "markets") return all.find((card) => card.kind === "chart" || card.kind === "sparkline");
  return all.find((card) => card.kind === "dollar");
}

function firmwareZones(layout) {
  if (layout === "btc_focus") return { z1: { x: 16, y: 48, w: 448, h: 432 }, z2: null };
  return {
    z1: { x: 16, y: 48, w: 448, h: 213 },
    z2: { x: 16, y: 267, w: 448, h: 213 },
  };
}

function applyCardNode(node, card) {
  const c = clampCard(card);
  node.style.left = `${(c.x / 480) * 100}%`;
  node.style.top = `${(c.y / 480) * 100}%`;
  node.style.width = `${(c.w / 480) * 100}%`;
  node.style.height = `${(c.h / 480) * 100}%`;
}

function beginCardPointer(event, id, resizing, node) {
  event.preventDefault();
  event.stopPropagation();
  selectCard(id, false);
  const card = selectedCard();
  if (!card) return;
  const rect = byId("screen-editor").getBoundingClientRect();
  const scale = 480 / rect.width;
  const start = { x: event.clientX, y: event.clientY, card: { ...card } };
  const move = (e) => {
    const dx = Math.round((e.clientX - start.x) * scale);
    const dy = Math.round((e.clientY - start.y) * scale);
    const patch = resizing
      ? { w: start.card.w + dx, h: start.card.h + dy }
      : { x: start.card.x + dx, y: start.card.y + dy };
    Object.assign(card, clampCard({ ...card, ...patch }));
    applyCardNode(node, card);
    renderInspector();
    renderScreen();
  };
  const up = () => {
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", up);
    renderEditor();
  };
  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", up);
}

function renderInspector() {
  const card = selectedCard();
  setText("selected-card", card ? card.id : "Sin seleccion");
  ["card-title", "card-kind", "card-source", "card-prompt", "card-x", "card-y", "card-w", "card-h", "card-accent"].forEach((id) => {
    byId(id).disabled = !card;
  });
  if (!card) return;
  setValue("card-title", card.title || "");
  setValue("card-kind", card.kind || "custom");
  setValue("card-source", card.source || "");
  setValue("card-prompt", card.prompt || "");
  setValue("card-x", card.x);
  setValue("card-y", card.y);
  setValue("card-w", card.w);
  setValue("card-h", card.h);
  setValue("card-accent", card.style?.accent || "greent");
}

function renderDeviceReadiness() {
  const m = state.manifest || fallbackManifest();
  const compatibility = m.compatibility || {};
  const active = activeDevice();
  setText("compatibility-state", compatibility.mode || (compatibility.firmwareSupported ? "device-supported" : "requires-firmware-runtime"));
  setText("required-keys-state", (m.requiredKeys || []).join(", ") || "none");
  renderDeviceSummary(active);
  if (active) {
    const host = active.host || active.ip || "";
    setText("device-settings-state", active.tokenRef ? "Lista" : "Conectada");
    const input = byId("device-host");
    if (host && (!input.value || input.value === "192.168.1.42")) input.value = host;
    return;
  }
  const current = byId("device-settings-state").textContent;
  if (!current || current === "no device" || current.startsWith("paired ") || current.startsWith("connected ") || current.startsWith("online ")) {
    setText("device-settings-state", "no device");
  }
}

function renderExperiences() {
  const list = byId("experience-list");
  list.replaceChildren();
  if (!state.experiences.length) {
    list.append(emptyRow("No forks yet"));
    return;
  }
  state.experiences.slice(0, 5).forEach((exp) => {
    const row = document.createElement("div");
    row.className = "experience-item";
    const label = document.createElement("div");
    const title = document.createElement("strong");
    const meta = document.createElement("span");
    title.textContent = exp.name || exp.id;
    meta.textContent = `${exp.cardCount} cards / ${exp.firmwareSupported ? "device" : "preview"}${exp.active ? " / active" : ""}`;
    label.append(title, meta);
    const button = document.createElement("button");
    button.className = "ghost-button";
    button.type = "button";
    button.textContent = exp.active ? "Active" : "Activate";
    button.disabled = exp.active;
    button.addEventListener("click", () => activateExperience(exp.id));
    row.append(label, button);
    list.append(row);
  });
}

function renderKeys() {
  const list = byId("key-list");
  list.replaceChildren();
  if (!state.keys.length) {
    list.textContent = "No hay keys guardadas.";
    return;
  }
  state.keys.forEach((key) => {
    const row = document.createElement("div");
    row.className = "mini-row";
    const name = document.createElement("strong");
    const masked = document.createElement("code");
    const button = document.createElement("button");
    button.className = "ghost-button";
    button.type = "button";
    name.textContent = key.name;
    masked.textContent = key.masked;
    button.textContent = "Borrar";
    button.addEventListener("click", () => deleteKey(key.name));
    row.append(name, masked, button);
    list.append(row);
  });
}

function renderJobs() {
  const list = byId("jobs-list");
  list.replaceChildren();
  if (!state.jobs.length) {
    list.textContent = "Sin operaciones todavia.";
    renderLatestJobState();
    return;
  }
  state.jobs.slice(0, 10).forEach((job) => {
    const item = document.createElement("div");
    item.className = "job";
    const head = document.createElement("div");
    const title = document.createElement("strong");
    const pct = document.createElement("code");
    const bar = document.createElement("span");
    const fill = document.createElement("i");
    const logs = document.createElement("pre");
    title.textContent = `${job.kind} / ${job.state}`;
    pct.textContent = `${job.progress || 0}%`;
    fill.style.width = `${job.progress || 0}%`;
    logs.textContent = job.error ? `${(job.logs || []).slice(-9).join("\n")}\nERROR: ${job.error}` : (job.logs || []).slice(-9).join("\n");
    head.append(title, pct);
    bar.append(fill);
    item.append(head, bar, logs);
    list.append(item);
  });
  renderLatestJobState();
}

function updateSelectedFromInspector() {
  const card = selectedCard();
  if (!card) return;
  Object.assign(card, clampCard({
    ...card,
    title: valueOf("card-title"),
    kind: valueOf("card-kind"),
    source: valueOf("card-source"),
    prompt: valueOf("card-prompt"),
    x: valueOf("card-x"),
    y: valueOf("card-y"),
    w: valueOf("card-w"),
    h: valueOf("card-h"),
    style: { ...(card.style || {}), accent: valueOf("card-accent") },
  }));
  card.id = card.id || slug(card.title);
  renderEditor();
  renderInspector();
  state.manifest = ensureExperience(state.manifest);
  renderManifestForm();
  renderDeviceReadiness();
  renderScreen();
}

function addCard(kind) {
  const card = newCard(kind, cards().length + 1);
  state.manifest.cards = [...cards(), card];
  state.selectedId = card.id;
  renderEditor();
  renderInspector();
  state.manifest = ensureExperience(state.manifest);
  renderManifestForm();
  renderDeviceReadiness();
  renderScreen();
}

function addSource(kind) {
  const id = uniqueId(kind.replace(".", "-"));
  const source = {
    id,
    title: kind.split(".").map((part) => part[0].toUpperCase() + part.slice(1)).join(" "),
    kind,
    keyNames: kind === "weather.current" ? ["WEATHER_API_KEY"] : [],
    config: defaultSourceConfig(kind),
  };
  state.manifest.sources = [...sources(), source];
  state.manifest = ensureExperience(state.manifest);
  state.selectedSourceId = id;
  renderSourceCatalog();
  renderSourceEditor();
  renderManifestForm();
  renderDeviceReadiness();
}

function applySourceToSelected(sourceId) {
  const card = selectedCard();
  if (card) {
    card.source = sourceId;
    state.selectedSourceId = sourceId;
    renderInspector();
    renderEditor();
    renderSourceEditor();
    renderScreen();
  }
}

function selectedSource() {
  return sources().find((item) => item.id === state.selectedSourceId);
}

function saveSourceConfig() {
  const source = selectedSource();
  if (!source) {
    setText("source-editor-state", "Elige una fuente");
    return;
  }
  source.config = { ...(source.config || {}) };
  if (source.kind === "market.crypto") {
    source.config.symbol = valueOf("source-symbol").trim().toUpperCase() || "BTC";
    source.config.currency = valueOf("source-currency").trim().toUpperCase() || "USD";
    source.title = `${source.config.symbol} / ${source.config.currency}`;
  } else if (source.kind === "market.stocks" || source.kind === "watchlist") {
    const symbols = parseList(valueOf("source-symbols")).map((item) => item.toUpperCase()).slice(0, 8);
    source.config.symbols = symbols.length ? symbols : ["AAPL", "TSLA", "NVDA"];
    source.title = "Watchlist";
    state.manifest.watchlist = source.config.symbols;
    setValue("manifest-watchlist", source.config.symbols.join(", "));
  } else if (source.kind === "weather.current") {
    source.config.city = valueOf("source-city").trim() || "Buenos Aires";
    source.title = `Weather / ${source.config.city}`;
  } else {
    source.config.url = valueOf("source-url").trim();
    source.config.jsonPath = valueOf("source-json-path").trim();
  }
  state.sourcePreviews[source.id] = null;
  state.manifest = ensureExperience(readManifestForm());
  renderSourceCatalog();
  renderSourceEditor();
  renderManifestForm();
  renderScreen();
  setText("source-editor-state", "Datos actualizados");
}

async function previewSelectedSource() {
  const source = sources().find((item) => item.id === state.selectedSourceId)
    || sources().find((item) => item.id === selectedCard()?.source)
    || sources()[0];
  if (!source) {
    setText("source-preview-state", "Sin fuente");
    return;
  }
  setText("source-preview-state", "Previsualizando");
  const preview = await api(API.sourcePreview, { method: "POST", body: { source } });
  state.sourcePreviews[source.id] = preview;
  setText("source-preview-state", preview.ok ? "Datos listos" : previewErrorLabel(preview.error));
  renderSourceCatalog();
  renderScreen();
}

function duplicateSelected() {
  const card = selectedCard();
  if (!card) return;
  const copy = clampCard({ ...structuredClone(card), id: uniqueId(`${card.id}-copy`), x: card.x + 18, y: card.y + 18 });
  state.manifest.cards = [...cards(), copy];
  state.manifest = ensureExperience(state.manifest);
  state.selectedId = copy.id;
  renderEditor();
  renderInspector();
  renderManifestForm();
  renderDeviceReadiness();
  renderScreen();
}

function deleteSelected() {
  state.manifest.cards = cards().filter((card) => card.id !== state.selectedId);
  state.manifest = ensureExperience(state.manifest);
  state.selectedId = cards()[0]?.id || "";
  renderEditor();
  renderInspector();
  renderManifestForm();
  renderDeviceReadiness();
  renderScreen();
}

function selectCard(id, rerender = true) {
  state.selectedId = id;
  const card = selectedCard();
  if (card?.source) state.selectedSourceId = card.source;
  if (rerender) {
    renderEditor();
    renderInspector();
    renderScreen();
  }
}

function readManifestForm() {
  const watchlist = valueOf("manifest-watchlist").split(/[,\n]/).map((item) => item.trim()).filter(Boolean);
  syncWatchlistSource(watchlist);
  return {
    ...state.manifest,
    name: valueOf("manifest-name"),
    theme: valueOf("manifest-theme"),
    layout: valueOf("manifest-layout"),
    watchlist,
    releaseNotes: valueOf("manifest-notes"),
    cards: cards().map((card) => clampCard(card)),
    sources: sources(),
    deviceSettings: {
      theme: valueOf("manifest-theme"),
      layout: valueOf("manifest-layout"),
      brightness: Number(valueOf("device-brightness")) || 255,
      z2Mode: valueOf("device-z2") || "usd",
      proMode: byId("device-pro-mode").checked,
      watchlist,
    },
  };
}

async function saveManifest() {
  state.manifest = ensureExperience(await api(API.manifest, { method: "POST", body: readManifestForm() }));
  state.selectedId = cards().find((card) => card.id === state.selectedId)?.id || cards()[0]?.id || "";
  renderAll();
  setText("manifest-state", "Preset guardado");
}

async function saveExperience() {
  const id = valueOf("experience-id").trim() || slug(valueOf("manifest-name")) || "draft";
  const saved = await api(`${API.experiences}/${id}`, { method: "PUT", body: readManifestForm() });
  setValue("experience-id", saved.id);
  const list = await api(API.experiences);
  state.experiences = list.experiences || [];
  renderExperiences();
}

async function activateExperience(id) {
  const activated = await api(`${API.experiences}/${id}/activate`, { method: "POST", body: {} });
  state.manifest = ensureExperience(activated.active.experience);
  const list = await api(API.experiences);
  state.experiences = list.experiences || [];
  renderAll();
}

async function createAgentDraft() {
  const prompt = valueOf("deploy-prompt").trim();
  if (!prompt) throw new Error("Escribi un prompt primero.");
  const job = await api(API.agentDraft, { method: "POST", body: { prompt, experience: readManifestForm() } });
  state.jobs.unshift(job);
  renderJobs();
  setText("deploy-state", "Agent draft escrito en artifacts");
}

async function createDeployDraft() {
  const prompt = valueOf("deploy-prompt").trim();
  if (!prompt) throw new Error("Escribi un prompt primero.");
  const job = await api(API.deployDraft, {
    method: "POST",
    body: { prompt, target: valueOf("deploy-target") || "firmware", manifest: readManifestForm(), keys: state.keys.map((key) => ({ name: key.name })) },
  });
  state.jobs.unshift(job);
  renderJobs();
  setText("deploy-state", "Deploy draft legacy escrito");
}

async function createJob(kind, payload = {}) {
  const job = await api(kind === "flash" ? API.flash : `${API.jobs}/${kind}`, { method: "POST", body: payload });
  state.jobs.unshift(job);
  renderJobs();
  setTimeout(refreshJobs, 350);
  return job;
}

async function checkRelease() {
  state.release = await api(API.release);
  renderStatus();
}

async function saveKey() {
  const saved = await api(API.keys, { method: "POST", body: { name: valueOf("key-name"), value: valueOf("key-value") } });
  state.keys = [saved, ...state.keys.filter((key) => key.name !== saved.name)];
  setValue("key-value", "");
  renderKeys();
}

async function deleteKey(name) {
  await api(`${API.keys}/delete`, { method: "POST", body: { name } });
  state.keys = state.keys.filter((key) => key.name !== name);
  renderKeys();
}

async function connectDevice() {
  const connected = await api(API.deviceConnect, { method: "POST", body: { host: valueOf("device-host") } });
  state.pairing = false;
  await refreshDevices();
  setText("device-settings-state", `Conectada ${connected.device.host}`);
}

async function pairDevice() {
  const code = valueOf("pair-code");
  const paired = await api(API.devicePair, { method: "POST", body: { host: valueOf("device-host"), code } });
  if (paired.code) {
    state.pairing = true;
    renderNav();
    setText("device-settings-state", `Codigo ${paired.code}`);
    setValue("pair-code", paired.code);
  } else {
    state.pairing = false;
    await refreshDevices();
    setText("device-settings-state", "Pairing listo");
  }
}

async function checkDeviceHealth() {
  await refreshDeviceHealth();
}

async function applyDeviceSettings() {
  const payload = readManifestForm().deviceSettings;
  try {
    const job = await api(API.syncSettings, { method: "POST", body: payload });
    state.jobs.unshift(job);
    renderJobs();
    await refreshDevices();
    setText("device-settings-state", "Aplicado");
  } catch (err) {
    if (isUnauthorized(err)) {
      await autoPairDevice();
      const job = await api(API.syncSettings, { method: "POST", body: payload });
      state.jobs.unshift(job);
      renderJobs();
      await refreshDevices();
      setText("device-settings-state", "Aplicado");
      return;
    }
    throw err;
  }
}

async function scanNetworkForDevice() {
  setText("device-settings-state", "Buscando...");
  const current = sanitizeDeviceHost(valueOf("device-host"));
  const prefixes = scanPrefixes(current);
  const candidates = [];
  prefixes.forEach((prefix) => {
    for (let i = 2; i < 255; i++) candidates.push(`${prefix}.${i}`);
  });
  const found = await scanCandidates(candidates);
  if (!found) throw new Error("No encontre la cajita. Proba ingresando la IP desde Settings.");
  setValue("device-host", found.host || found.ip);
  await connectDevice();
}

async function refreshDevices() {
  const devices = await api(API.devices);
  state.devices = devices.devices || [];
  renderStatus();
  renderDeviceReadiness();
}

async function refreshDeviceHealth({ silent = false } = {}) {
  try {
    state.deviceHealth = await api(API.deviceHealth);
    await refreshDevices();
    renderDeviceReadiness();
    setText("device-settings-state", "Conectada");
  } catch (err) {
    state.deviceHealth = { ok: false, error: err.message };
    renderDeviceReadiness();
    if (!silent) setText("device-settings-state", err.message);
  }
}

function selectedFlashPayload() {
  const port = valueOf("port-select");
  if (!port) throw new Error("Selecciona un puerto USB.");
  if (state.flashMode === "recovery" && !byId("recovery-ack").checked) {
    throw new Error("Recovery requiere marcar la confirmacion de borrado.");
  }
  return {
    port,
    mode: state.flashMode,
    firmwareSource: valueOf("firmware-source"),
    firmwarePath: valueOf("firmware-path"),
    confirmed: true,
    recoveryConfirmed: state.flashMode === "recovery",
  };
}

async function runFlashFlow() {
  const payload = selectedFlashPayload();
  const signature = `${payload.mode}:${payload.port}:${payload.firmwareSource}:${payload.firmwarePath}`;
  if (state.flashArm !== signature) {
    state.flashArm = signature;
    setText("flash-state", payload.mode === "recovery"
      ? "Recovery armado. Click otra vez para borrar y reescribir."
      : "Flash normal armado. Click otra vez para escribir firmware.bin.");
    return;
  }
  state.flashArm = "";
  setText("flash-state", "Iniciando flash...");
  await createJob("flash", payload);
}

async function runOta(kind) {
  const signature = `${kind}:${state.status?.localBuild?.firmwarePath || "local"}`;
  if (state.otaArm !== signature) {
    state.otaArm = signature;
    setText("flash-state", kind === "ota-upload"
      ? "Wireless OTA local armado. Click otra vez para subir firmware/app."
      : "Wireless OTA latest armado. Click otra vez para instalar release.");
    return;
  }
  state.otaArm = "";
  const endpoint = kind === "ota-upload" ? API.otaUpload : API.otaLatest;
  const job = await api(endpoint, { method: "POST", body: { confirmed: true } });
  state.jobs.unshift(job);
  renderJobs();
  setText("flash-state", "OTA job queued");
}

async function runSimpleOtaLatest() {
  const signature = `simple-ota:${activeDevice()?.host || valueOf("device-host")}`;
  if (state.otaArm !== signature) {
    state.otaArm = signature;
    setText("flash-state", "Actualizacion armada. Toca otra vez para instalar.");
    return;
  }
  state.otaArm = "";
  setText("flash-state", "Instalando actualizacion...");
  try {
    if (STATIC_STUDIO) {
      const result = await webDeviceFetch("/api/ota/github", { method: "POST", body: {}, token: true });
      setText("flash-state", result.rebooting ? "Actualizando. La cajita se reinicia." : "No hay actualizacion disponible.");
      return;
    }
    await runOta("ota-latest");
  } catch (err) {
    if (isUnauthorized(err)) {
      await autoPairDevice();
      setText("flash-state", "Vinculada. Toca actualizar otra vez.");
      return;
    }
    throw err;
  }
}

function bindEvents() {
  byId("refresh-all").addEventListener("click", refreshAll);
  byId("advanced-toggle").addEventListener("click", toggleAdvancedMode);
  byId("scan-job").addEventListener("click", () => createJob("detect").then(refreshAll).catch(showServerError));
  byId("save-manifest").addEventListener("click", () => saveManifest().catch((err) => setText("manifest-state", err.message)));
  byId("save-experience").addEventListener("click", () => saveExperience().catch((err) => setText("manifest-state", err.message)));
  byId("agent-draft-job").addEventListener("click", () => createAgentDraft().catch((err) => setText("deploy-state", err.message)));
  byId("deploy-draft-job").addEventListener("click", () => createDeployDraft().catch((err) => setText("deploy-state", err.message)));
  byId("preview-source").addEventListener("click", () => previewSelectedSource().catch((err) => setText("source-preview-state", err.message)));
  byId("save-source-config").addEventListener("click", () => saveSourceConfig());
  byId("save-key").addEventListener("click", () => saveKey().catch((err) => setText("key-state", err.message)));
  byId("connect-device").addEventListener("click", () => connectDevice().catch((err) => setText("device-settings-state", err.message)));
  byId("health-device").addEventListener("click", () => checkDeviceHealth().catch((err) => setText("device-settings-state", err.message)));
  byId("apply-device-settings").addEventListener("click", () => applyDeviceSettings().catch((err) => setText("device-settings-state", err.message)));
  byId("scan-network").addEventListener("click", () => scanNetworkForDevice().catch((err) => setText("device-settings-state", err.message)));
  byId("ota-latest-simple").addEventListener("click", () => runSimpleOtaLatest().catch((err) => setText("flash-state", err.message)));
  byId("release-job").addEventListener("click", () => checkRelease().catch(showServerError));
  byId("download-job").addEventListener("click", () => createJob("download-release").catch(showServerError));
  byId("build-job").addEventListener("click", () => createJob("build").then(refreshAll).catch(showServerError));
  byId("verify-action").addEventListener("click", () => createJob("verify-boot").catch((err) => setText("flash-state", err.message)));
  byId("flash-action").addEventListener("click", () => runFlashFlow().catch((err) => setText("flash-state", err.message)));
  byId("ota-upload-action").addEventListener("click", () => runOta("ota-upload").catch((err) => setText("flash-state", err.message)));
  byId("ota-latest-action").addEventListener("click", () => runOta("ota-latest").catch((err) => setText("flash-state", err.message)));
  byId("ota-verify-action").addEventListener("click", () => createJob("ota-verify", { seconds: 12 }).catch((err) => setText("flash-state", err.message)));
  byId("health-device-technical").addEventListener("click", () => checkDeviceHealth().catch((err) => setText("flash-state", err.message)));
  byId("duplicate-card").addEventListener("click", duplicateSelected);
  byId("delete-card").addEventListener("click", deleteSelected);
  byId("screen-focus").addEventListener("click", () => setCameraView("screen"));
  document.querySelectorAll(".nav-button").forEach((button) => button.addEventListener("click", () => {
    state.activePanel = button.dataset.panel;
    renderNav();
  }));
  document.querySelectorAll("[data-demo-screen]").forEach((button) => button.addEventListener("click", () => {
    state.demoScreen = button.dataset.demoScreen || "home";
    renderScreen();
  }));
  document.querySelectorAll(".view-button").forEach((button) => button.addEventListener("click", () => setCameraView(button.dataset.view)));
  byId("firmware-source").addEventListener("input", () => byId("custom-path-row").hidden = valueOf("firmware-source") !== "path");
  byId("assembly-slider").addEventListener("input", () => state.workbench?.setAssemblyProgress(Number(valueOf("assembly-slider")) / 100));
  document.querySelectorAll(".flash-mode").forEach((button) => button.addEventListener("click", () => {
    state.flashMode = button.dataset.mode;
    state.flashArm = "";
    document.querySelectorAll(".flash-mode").forEach((item) => item.classList.toggle("active", item === button));
  }));
  ["manifest-name", "manifest-theme", "manifest-layout", "manifest-watchlist", "manifest-notes", "device-brightness", "device-z2", "device-pro-mode"].forEach((id) => {
    byId(id).addEventListener("input", () => {
      state.manifest = ensureExperience(readManifestForm());
      renderManifestForm();
      renderDeviceReadiness();
      renderScreen();
    });
  });
  ["card-title", "card-kind", "card-source", "card-prompt", "card-x", "card-y", "card-w", "card-h", "card-accent"].forEach((id) => {
    byId(id).addEventListener("input", updateSelectedFromInspector);
  });
}

function setCameraView(view) {
  state.workbench?.setCameraView(view);
  document.querySelectorAll(".view-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.view === view);
  });
}

function toggleAdvancedMode() {
  state.advanced = !state.advanced;
  if (!state.advanced && state.activePanel === "flash") state.activePanel = "device";
  renderAll();
}

function visibleCardKinds() {
  const simple = ["btc", "dollar", "stocks", "text"];
  return state.advanced ? Object.keys(CARD_TEMPLATES) : simple.filter((kind) => CARD_TEMPLATES[kind]);
}

function ensureExperience(manifest = {}) {
  const fallback = fallbackManifest();
  const next = {
    ...fallback,
    ...manifest,
    cards: (Array.isArray(manifest.cards) ? manifest.cards : fallback.cards).map(clampCard),
    sources: Array.isArray(manifest.sources) ? manifest.sources : fallback.sources,
  };
  next.deviceSettings = {
    ...fallback.deviceSettings,
    ...(manifest.deviceSettings || {}),
    theme: next.theme,
    layout: next.layout,
    watchlist: next.watchlist,
  };
  next.requiredKeys = collectRequiredKeys(next.sources);
  next.compatibility = computeCompatibility(next.cards);
  return next;
}

function fallbackManifest() {
  const cards = [newCard("btc", 1), newCard("dollar", 2), newCard("stocks", 3)];
  return {
    experienceVersion: 1,
    basePreset: "lemon-default",
    name: "Lemon Box Studio",
    theme: "dark",
    layout: "btc_usd",
    watchlist: ["AAPL", "TSLA", "NVDA"],
    cards,
    sources: [
      { id: "btc-usd", title: "BTC / USD", kind: "market.crypto", config: { symbol: "BTC", currency: "USD" }, keyNames: [] },
      { id: "watchlist", title: "Watchlist", kind: "market.stocks", config: { symbols: ["AAPL", "TSLA", "NVDA"] }, keyNames: [] },
    ],
    requiredKeys: [],
    deviceSettings: { theme: "dark", layout: "btc_usd", brightness: 255, z2Mode: "usd", proMode: false, watchlist: ["AAPL", "TSLA", "NVDA"] },
    compatibility: { firmwareSupported: true, mode: "device-supported", unsupportedCards: [] },
    runtimeSchemaVersion: 1,
    deviceBundle: { schemaVersion: 1, cards: [] },
    syncStatus: "local-draft",
    lastAppliedDeviceId: "",
    releaseNotes: "",
  };
}

function defaultSourceConfig(kind) {
  if (kind === "market.crypto") return { symbol: "BTC", currency: "USD" };
  if (kind === "market.stocks") return { symbols: ["AAPL", "TSLA", "NVDA"] };
  if (kind === "weather.current") return { city: "Buenos Aires" };
  return { url: "http://127.0.0.1/data.json", jsonPath: "data.value" };
}

function selectedCard() {
  return cards().find((card) => card.id === state.selectedId);
}

function cards() {
  if (!state.manifest) state.manifest = fallbackManifest();
  if (!Array.isArray(state.manifest.cards)) state.manifest.cards = [];
  return state.manifest.cards;
}

function sources() {
  if (!state.manifest) state.manifest = fallbackManifest();
  if (!Array.isArray(state.manifest.sources)) state.manifest.sources = [];
  return state.manifest.sources;
}

function collectRequiredKeys(items) {
  return [...new Set((items || []).flatMap((item) => item.keyNames || []))];
}

function computeCompatibility(items) {
  const supported = new Set(["btc", "dollar", "stocks", "watchlist", "chart", "metric", "text", "list", "sparkline"]);
  const unsupported = (items || []).filter((card) => !supported.has(card.kind)).map((card) => card.id);
  return {
    firmwareSupported: unsupported.length === 0,
    mode: unsupported.length ? "requires-firmware-runtime" : "device-supported",
    deviceSettingsOnly: unsupported.length > 0,
    unsupportedCards: unsupported,
  };
}

function uniqueId(base) {
  const ids = new Set([...cards().map((card) => card.id), ...sources().map((source) => source.id)]);
  const seed = slug(base);
  let id = seed;
  let idx = 2;
  while (ids.has(id)) id = `${seed}-${idx++}`;
  return id;
}

function showServerError(err) {
  setText("server-state", err.message);
}

async function staticApi(path, options = {}) {
  const method = (options.method || "GET").toUpperCase();
  const body = options.body || {};
  if (path === API.status) {
    return {
      appVersion: "web-preview",
      ports: [],
      localBuild: { ready: false },
      recoveryAssets: { ready: false, files: {} },
    };
  }
  if (path === API.manifest && method === "GET") return staticManifest();
  if (path === API.manifest && method === "POST") {
    localStorage.setItem("lemon-studio-manifest", JSON.stringify(body));
    return body;
  }
  if (path === API.keys) return { keys: [] };
  if (path === API.capabilities) return staticCapabilities();
  if (path === API.sources) return staticSources();
  if (path === API.experiences) return { experiences: [] };
  if (path === API.devices) {
    const device = storedWebDevice();
    return { devices: device ? [device] : [] };
  }
  if (path === API.jobs) return { jobs: state.jobs || [] };
  if (path === API.sourcePreview && method === "POST") return staticSourcePreview(body.source || body);
  if (path === API.deviceConnect && method === "POST") return connectWebDevice(body.host || body.ip);
  if (path === API.devicePair && method === "POST") return pairWebDevice(body.code);
  if (path === API.deviceHealth) return webDeviceFetch("/api/health");
  if (path === API.deviceSettings && method === "GET") return webDeviceFetch("/api/settings", { token: true });
  if (path === API.deviceSettings && method === "POST") return webDeviceFetch("/api/settings", { method: "POST", body, token: true });
  if (path === API.syncSettings && method === "POST") {
    const result = await webDeviceFetch("/api/settings", { method: "POST", body, token: true });
    const job = {
      id: `web-sync-${Date.now()}`,
      kind: "sync-settings",
      state: "complete",
      progress: 100,
      logs: ["Settings applied over WiFi"],
      result,
      createdAt: Date.now() / 1000,
      updatedAt: Date.now() / 1000,
    };
    state.jobs = [job, ...(state.jobs || [])].slice(0, 10);
    return job;
  }
  if (path === API.syncSettings || path === API.otaUpload || path === API.otaLatest || path === API.otaVerify || path === API.flash || path.startsWith(`${API.jobs}/`)) {
    throw new Error("Esta accion requiere el Studio local con backend.");
  }
  if (path.includes("/api/device/")) throw new Error("Esta accion requiere el Studio local.");
  throw new Error("Disponible solo en Studio local.");
}

function deviceFromQuery() {
  if (!STATIC_STUDIO) return;
  const ip = new URLSearchParams(location.search).get("device");
  if (!ip) return;
  const input = byId("device-host");
  if (input && !input.value) input.value = sanitizeDeviceHost(ip);
}

function sanitizeDeviceHost(value) {
  return String(value || "")
    .trim()
    .replace(/^https?:\/\//, "")
    .replace(/\/.*$/, "")
    .replace(/[^a-zA-Z0-9.:-]/g, "");
}

function storedWebDevice() {
  try {
    const saved = JSON.parse(localStorage.getItem(WEB_DEVICE_KEY) || "null");
    if (!saved?.host) return null;
    return { ...saved, tokenRef: webDeviceToken(saved.id || saved.host) ? "browser-local" : "" };
  } catch (err) {
    localStorage.removeItem(WEB_DEVICE_KEY);
    return null;
  }
}

function saveWebDevice(device, token = "") {
  const host = sanitizeDeviceHost(device.host || device.ip || valueOf("device-host"));
  const id = device.id || device.mac || host;
  const safe = {
    id,
    name: device.name || "Lemon Box",
    host,
    ip: device.ip || host,
    version: device.version || "",
    hardware: device.hardware || "esp32-s3",
    mac: device.mac || "",
    paired: !!device.paired || !!token,
    lastSeenAt: new Date().toISOString(),
  };
  localStorage.setItem(WEB_DEVICE_KEY, JSON.stringify(safe));
  if (token) localStorage.setItem(`${WEB_DEVICE_TOKEN_PREFIX}${id}`, token);
  return { ...safe, tokenRef: webDeviceToken(id) ? "browser-local" : "" };
}

function webDeviceToken(id) {
  if (!id) return "";
  return localStorage.getItem(`${WEB_DEVICE_TOKEN_PREFIX}${id}`) || "";
}

function isUnauthorized(err) {
  const text = String(err?.message || err).toLowerCase();
  return text.includes("unauthorized") || text.includes("vincular");
}

async function startPairingAfterUnauthorized() {
  return autoPairDevice();
}

async function autoPairDevice(host = "") {
  const active = storedWebDevice();
  const targetHost = sanitizeDeviceHost(host || active?.host || valueOf("device-host"));
  if (!targetHost) throw new Error("Conecta la cajita primero.");
  state.pairing = true;
  renderNav();
  setText("device-settings-state", "Vinculando...");
  const started = await webDeviceFetch("/api/pair/start", { method: "POST", host: targetHost, auth: false });
  if (!started.code) throw new Error("La cajita no entrego codigo de vinculacion.");
  setValue("pair-code", started.code);
  const paired = await webDeviceFetch("/api/pair/confirm", {
    method: "POST",
    host: targetHost,
    body: { code: started.code },
    auth: false,
  });
  const saved = saveWebDevice({ ...paired, host: targetHost }, paired.token || "");
  state.pairing = false;
  state.devices = [saved];
  renderNav();
  renderDeviceReadiness();
  setText("device-settings-state", "Vinculada");
  return { ok: true, device: saved };
}

async function connectWebDevice(host) {
  const cleaned = sanitizeDeviceHost(host);
  if (!cleaned) throw new Error("Ingresa la IP local de la cajita.");
  const device = await webDeviceFetch("/api/device", { host: cleaned, auth: false });
  const saved = saveWebDevice({ ...device, host: cleaned });
  return { ok: true, device: saved };
}

async function pairWebDevice(code) {
  const active = storedWebDevice();
  const host = active?.host || sanitizeDeviceHost(valueOf("device-host"));
  if (!host) throw new Error("Conecta la cajita primero.");
  if (!code) return autoPairDevice(host);
  const paired = await webDeviceFetch("/api/pair/confirm", { method: "POST", host, body: { code }, auth: false });
  const saved = saveWebDevice({ ...paired, host }, paired.token || "");
  return { ok: true, device: saved };
}

async function webDeviceFetch(route, { method = "GET", host = "", body = null, token = false, auth = true } = {}) {
  const active = storedWebDevice();
  const targetHost = sanitizeDeviceHost(host || active?.host || valueOf("device-host"));
  if (!targetHost) throw new Error("Ingresa la IP local de la cajita.");
  const headers = { Accept: "application/json" };
  if (body) headers["Content-Type"] = "application/json";
  if (auth && token !== false) {
    const bearer = webDeviceToken(active?.id || targetHost);
    if (bearer) headers.Authorization = `Bearer ${bearer}`;
  }
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 6500);
  const init = {
    method,
    headers,
    signal: controller.signal,
    targetAddressSpace: "local",
  };
  if (body) init.body = JSON.stringify(body);
  try {
    const res = await fetch(`http://${targetHost}${route}`, init);
    const text = await res.text();
    const payload = text ? JSON.parse(text) : {};
    if (!res.ok) throw new Error(payload.error === "unauthorized" ? "Necesitas vincular esta cajita." : (payload.error || `HTTP ${res.status}`));
    if (route === "/api/health" || route === "/api/settings") saveWebDevice({ ...(active || {}), host: targetHost, ...payload });
    return payload;
  } catch (err) {
    if (err.name === "AbortError") throw new Error("La cajita no respondio en la red local.");
    if (String(err.message || err).includes("Failed to fetch")) {
      throw new Error("Chrome bloqueo o no pudo acceder a la red local. Revisa permiso Local Network, IP y CORS/LNA del firmware.");
    }
    throw err;
  } finally {
    clearTimeout(timeout);
  }
}

function scanPrefixes(currentHost) {
  const parts = String(currentHost || "").split(".");
  const prefixes = [];
  if (parts.length >= 3 && parts.slice(0, 3).every((part) => /^\d+$/.test(part))) {
    prefixes.push(parts.slice(0, 3).join("."));
  }
  ["192.168.1", "192.168.0", "10.0.0"].forEach((prefix) => {
    if (!prefixes.includes(prefix)) prefixes.push(prefix);
  });
  return prefixes;
}

async function scanCandidates(candidates) {
  const chunkSize = 18;
  for (let i = 0; i < candidates.length; i += chunkSize) {
    const chunk = candidates.slice(i, i + chunkSize);
    setText("device-settings-state", `Buscando ${chunk[0]}...`);
    const found = await Promise.any(chunk.map((host) => probeDeviceHost(host))).catch(() => null);
    if (found) return found;
  }
  return null;
}

async function probeDeviceHost(host) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 900);
  try {
    const res = await fetch(`http://${host}/api/device`, {
      headers: { Accept: "application/json" },
      signal: controller.signal,
      targetAddressSpace: "local",
    });
    if (!res.ok) throw new Error("not device");
    const payload = await res.json();
    if (payload?.name !== "Lemon Box" && !payload?.id) throw new Error("not lemon box");
    return { ...payload, host };
  } finally {
    clearTimeout(timeout);
  }
}

function staticManifest() {
  try {
    const saved = JSON.parse(localStorage.getItem("lemon-studio-manifest") || "null");
    if (saved) return saved;
  } catch (err) {
    localStorage.removeItem("lemon-studio-manifest");
  }
  return fallbackManifest();
}

function staticCapabilities() {
  return {
    mode: "web-direct",
    deviceActions: true,
    firmwareActions: false,
    sourcePreview: true,
    note: "La version web puede conectar por WiFi y aplicar settings soportados. OTA, flash y USB requieren Studio local.",
  };
}

function staticSources() {
  return {
    catalog: [
      { kind: "market.crypto", title: "Crypto market", firmwareSupported: true, requiredKeys: [] },
      { kind: "market.stocks", title: "Stocks watchlist", firmwareSupported: true, requiredKeys: [] },
      { kind: "weather.current", title: "Weather", firmwareSupported: false, requiredKeys: [] },
      { kind: "custom.http_json", title: "Custom HTTP JSON", firmwareSupported: false, requiredKeys: [] },
    ],
    configured: sources().map((source) => ({
      id: source.id,
      title: source.title,
      kind: source.kind,
      requiredKeys: source.keyNames || [],
      firmwareSupported: source.kind === "market.crypto" || source.kind === "market.stocks",
      health: "idle",
      lastPreview: null,
      error: null,
      missingKeys: [],
    })),
  };
}

function staticSourcePreview(source = {}) {
  const kind = source.kind || "market.crypto";
  if (kind === "market.stocks" || kind === "watchlist") {
    const symbols = source.config?.symbols || ["AAPL", "TSLA", "NVDA"];
    return {
      ok: true,
      sourceId: source.id || "watchlist",
      kind,
      sample: {
        symbols,
        rows: symbols.slice(0, 5).map((symbol, index) => ({
          symbol,
          price: [212.4, 328.7, 146.2, 624.8, 182.3][index] || 100 + index * 17,
          change: [1.2, -0.8, 2.1, 0.4, -1.1][index] || 0.7,
        })),
        provider: "web-demo",
        partial: false,
      },
      fields: ["rows", "symbols"],
      requiredKeys: [],
      missingKeys: [],
      firmwareSupported: true,
      error: null,
      cached: false,
      fetchedAt: Math.floor(Date.now() / 1000),
    };
  }
  return {
    ok: true,
    sourceId: source.id || "btc-usd",
    kind,
    sample: {
      symbol: source.config?.symbol || "BTC",
      currency: source.config?.currency || "USD",
      price: 65260,
      change24h: 3.4,
      provider: "web-demo",
    },
    fields: ["price", "symbol", "currency"],
    requiredKeys: [],
    missingKeys: [],
    firmwareSupported: true,
    error: null,
    cached: false,
    fetchedAt: Math.floor(Date.now() / 1000),
  };
}

function renderLatestJobState() {
  const job = state.jobs[0];
  if (!job) return;
  if (job.kind === "sync-settings") {
    renderSyncJobState(job);
    return;
  }
  if (job.kind === "ota-upload" || job.kind === "ota-latest" || job.kind === "ota-verify") {
    renderOtaJobState(job);
  }
}

function activeDevice() {
  return state.devices[0] || null;
}

function deviceBadgeText(ports, active) {
  if (ports.length && active) return `${ports.length} USB / WiFi`;
  if (ports.length) return `${ports.length} USB`;
  if (active) return "WiFi device";
  return "Sin device";
}

function renderDeviceSummary(active) {
  const health = state.deviceHealth || {};
  if (!active) {
    setText("device-connection-label", "Sin conectar");
    setText("device-connection-detail", "Agrega la IP local de la cajita.");
    setText("device-signal-label", "--");
    setText("device-signal-detail", "Pendiente");
    setText("device-version-label", "--");
    setText("device-uptime-label", "Sin datos");
    setText("device-sync-label", "No aplicada");
    setText("device-sync-detail", "Solo settings soportados.");
    return;
  }

  const host = active.host || active.ip || active.id;
  const connected = health.ok !== false;
  setText("device-connection-label", connected ? "Conectada" : "Sin respuesta");
  setText("device-connection-detail", host);
  setText("device-version-label", health.version || active.version || "--");
  setText("device-uptime-label", health.uptimeMs ? `Encendida ${formatDuration(health.uptimeMs)}` : "Health pendiente");

  const signal = wifiSignal(health.wifiRssi);
  setText("device-signal-label", signal.label);
  setText("device-signal-detail", signal.detail);

  setText("device-sync-label", state.lastSyncAt ? "Sincronizada" : "No aplicada");
  setText("device-sync-detail", state.lastSyncAt || "Solo settings soportados.");
}

function sourcePreviewMeta(preview) {
  if (!preview?.sample) return "";
  const sample = preview.sample;
  const provider = sample.provider || sample.meta?.provider || "";
  const partial = sample.partial || sample.meta?.partial;
  const parts = [provider, partial ? "partial" : ""].filter(Boolean);
  return parts.length ? ` / ${parts.join(" / ")}` : "";
}

function sourceKindLabel(kind) {
  if (kind === "market.crypto") return "Crypto";
  if (kind === "market.stocks" || kind === "watchlist") return "Acciones";
  if (kind === "weather.current") return "Clima";
  return "Custom";
}

function sourceHealthLabel(preview, fallback) {
  if (!preview) return "sin probar";
  if (preview.ok) return "listo";
  if (preview.missingKeys?.length) return "falta key";
  return fallback === "needs-attention" ? "revisar" : "sin probar";
}

function previewErrorLabel(error) {
  const text = String(error || "No se pudo previsualizar");
  if (text.includes("missing required key")) return "Falta una key";
  if (text.includes("not reachable") || text.includes("timed out")) return "No responde la fuente";
  return text;
}

function sourceEditorKind(kind) {
  if (kind === "market.crypto") return "crypto";
  if (kind === "market.stocks" || kind === "watchlist") return "stocks";
  if (kind === "weather.current") return "weather";
  return "custom";
}

function parseList(value) {
  return String(value || "").split(/[,\n]/).map((item) => item.trim()).filter(Boolean);
}

function renderSyncJobState(job) {
  if (job.state === "complete") {
    const active = activeDevice();
    state.lastSyncAt = formatClock(job.completedAt || Date.now() / 1000);
    renderDeviceSummary(active);
    setText("device-settings-state", "Sincronizada");
  } else if (job.state === "failed") {
    setText("device-settings-state", `Error de sync: ${job.error || "error"}`);
  } else {
    setText("device-settings-state", "Sincronizando");
  }
}

function wifiSignal(rssi) {
  if (typeof rssi !== "number") return { label: "--", detail: "Health pendiente" };
  if (rssi >= -60) return { label: "Buena", detail: `${rssi} dBm` };
  if (rssi >= -70) return { label: "Media", detail: `${rssi} dBm` };
  return { label: "Baja", detail: `${rssi} dBm` };
}

function formatDuration(ms) {
  const totalSeconds = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  if (hours > 0) return `${hours}h ${minutes}m`;
  return `${minutes}m`;
}

function formatClock(seconds) {
  const timestamp = Number(seconds || 0) * 1000;
  return new Date(timestamp).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function renderOtaJobState(job) {
  if (job.kind === "ota-verify" && job.state === "complete") {
    setText("flash-state", "OTA health verified");
  } else if (job.kind === "ota-upload" && job.state === "complete") {
    setText("flash-state", "OTA local complete. Verify health next.");
  } else if (job.kind === "ota-latest" && job.state === "complete") {
    setText("flash-state", "OTA latest complete. Verify health next.");
  } else if (job.state === "failed") {
    setText("flash-state", `${job.kind} failed: ${job.error || "error"}`);
  } else {
    setText("flash-state", `${job.kind} ${job.state}`);
  }
}

function byId(id) {
  return document.getElementById(id);
}

function setText(id, value) {
  byId(id).textContent = value;
}

function setValue(id, value) {
  byId(id).value = value ?? "";
}

function valueOf(id) {
  return byId(id).value;
}

function slug(value) {
  return String(value || "card").trim().toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "card";
}

boot();
