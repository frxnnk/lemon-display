const ZONES = ["hero", "top-left", "top-right", "bottom-left", "bottom-right", "side"];
const TYPES = ["btc", "dollar", "stocks", "weather", "custom", "text"];

export const el = (id) => document.getElementById(id);

export function renderStatus(state) {
  const s = state.status || {};
  const ports = s.ports || [];
  const portSelect = el("port-select");
  if (portSelect) {
    const current = portSelect.value;
    portSelect.replaceChildren();
    if (!ports.length) {
      portSelect.append(new Option("No USB devices detected", ""));
    } else {
      ports.forEach((port) => {
        portSelect.append(new Option(`${port.device} - ${port.description}`, port.device));
      });
      if (current) portSelect.value = current;
    }
  }

  setText("device-badge", ports.length ? `${ports.length} USB device(s)` : "No device");
  setText("app-version", s.appVersion || "--");
  setText("local-build", s.localBuild && s.localBuild.ready ? `${Math.round(s.localBuild.size / 1024)} KB` : "missing");
  setText("recovery-ready", s.recoveryAssets && s.recoveryAssets.ready ? "ready" : "missing");
  setText("latest-release", state.release ? `v${state.release.version}` : "--");
}

export function renderManifest(state, handlers, options = {}) {
  const m = state.manifest || {};
  if (!options.preserveForm) {
    setValue("manifest-name", m.name || "");
    setValue("manifest-theme", m.theme || "dark");
    setValue("manifest-layout", m.layout || "btc_usd");
    setValue("manifest-watchlist", (m.watchlist || []).join(", "));
    setValue("manifest-notes", m.releaseNotes || "");
    setText("manifest-state", "Manifest loaded from local Studio state.");
  }
  renderDisplaySummary();
  renderWindows(state, handlers);
  renderSceneWindows(state);
}

export function renderDisplaySummary() {
  const theme = valueOf("manifest-theme") || "dark";
  const layout = valueOf("manifest-layout") || "btc_usd";
  setText("preview-theme", theme === "light" ? "Light preset" : "Dark preset");
  setText("preview-mode", layout === "btc_focus" ? "BTC focus" : "BTC + USD");
}

export function renderKeys(state, onDelete) {
  const list = el("key-list");
  if (!list) return;
  list.replaceChildren();
  if (!state.keys.length) {
    list.textContent = "No keys saved.";
    return;
  }
  state.keys.forEach((key) => {
    const row = document.createElement("div");
    row.className = "key-row";
    const label = document.createElement("strong");
    const masked = document.createElement("code");
    const remove = document.createElement("button");
    label.textContent = key.name;
    masked.textContent = key.masked;
    remove.className = "button ghost";
    remove.type = "button";
    remove.textContent = "Delete";
    remove.addEventListener("click", () => onDelete(key.name));
    row.append(label, masked, remove);
    list.append(row);
  });
}

export function renderJobs(state) {
  const list = el("jobs-list");
  if (!list) return;
  list.replaceChildren();
  if (!state.jobs.length) {
    list.textContent = "No operations yet.";
    return;
  }
  state.jobs.slice(0, 8).forEach((job) => {
    const item = document.createElement("div");
    item.className = "job";
    const title = document.createElement("div");
    title.className = "job-title";
    const left = document.createElement("strong");
    const right = document.createElement("code");
    left.textContent = `${job.kind} - ${job.state}`;
    right.textContent = `${job.progress || 0}%`;
    title.append(left, right);

    const bar = document.createElement("div");
    bar.className = "bar";
    const fill = document.createElement("span");
    fill.style.width = `${job.progress || 0}%`;
    bar.append(fill);

    const pre = document.createElement("pre");
    const logs = (job.logs || []).slice(-10).join("\n");
    pre.textContent = job.error ? `${logs}\nERROR: ${job.error}` : logs;
    item.append(title, bar, pre);
    list.append(item);
  });
}

function renderWindows(state, handlers) {
  const list = el("window-list");
  if (!list) return;
  const windows = currentWindows(state);
  list.replaceChildren();
  if (!windows.length) {
    list.textContent = "No display windows yet.";
    return;
  }

  windows.forEach((windowDef, index) => {
    const row = document.createElement("div");
    row.className = "window-row";
    row.append(
      textInput(windowDef.title, "Title", "Window title", (value) => handlers.updateWindow(index, { title: value, id: slug(value) })),
      selectInput(windowDef.type, TYPES, "Window type", (value) => handlers.updateWindow(index, { type: value })),
      selectInput(windowDef.zone, ZONES, "Window zone", (value) => handlers.updateWindow(index, { zone: value })),
      textInput(windowDef.source, "Source", "Window source", (value) => handlers.updateWindow(index, { source: value })),
      removeButton(windowDef.title || windowDef.id || "window", () => handlers.removeWindow(index)),
    );
    list.append(row);
  });
}

function renderSceneWindows(state) {
  const scene = el("scene-windows");
  if (!scene) return;
  scene.replaceChildren();
  currentWindows(state).slice(0, 6).forEach((windowDef) => {
    const tile = document.createElement("div");
    tile.className = "scene-window";
    tile.dataset.zone = displayZone(windowDef.zone);
    const title = document.createElement("strong");
    const meta = document.createElement("span");
    title.textContent = windowDef.title || windowDef.id || "Window";
    meta.textContent = `${windowDef.type || "custom"} / ${windowDef.source || "local"}`;
    tile.append(title, meta);
    scene.append(tile);
  });
}

function currentWindows(state) {
  return (state.manifest && state.manifest.windows) || [];
}

function textInput(value, placeholder, label, onInput) {
  const input = document.createElement("input");
  input.value = value || "";
  input.placeholder = placeholder;
  input.setAttribute("aria-label", label);
  input.name = `window_${slug(placeholder)}`;
  input.autocomplete = "off";
  input.addEventListener("input", () => onInput(input.value));
  return input;
}

function selectInput(value, options, label, onInput) {
  const select = document.createElement("select");
  select.setAttribute("aria-label", label);
  select.name = `window_${options[0]}`;
  options.forEach((option) => select.append(new Option(option, option)));
  if (value && !options.includes(value)) select.append(new Option(value, value));
  select.value = value || options[0];
  select.addEventListener("input", () => onInput(select.value));
  return select;
}

function removeButton(label, onClick) {
  const button = document.createElement("button");
  button.className = "button ghost";
  button.type = "button";
  button.textContent = "Remove";
  button.setAttribute("aria-label", `Remove ${label}`);
  button.addEventListener("click", onClick);
  return button;
}

export function bind(id, event, handler) {
  const target = el(id);
  if (target) target.addEventListener(event, handler);
}

export function toggleCustomPath() {
  const row = el("custom-path-row");
  if (row) row.style.display = valueOf("firmware-source") === "path" ? "grid" : "none";
}

export function setText(id, text) {
  const target = el(id);
  if (target) target.textContent = text;
}

export function setValue(id, value) {
  const target = el(id);
  if (target) target.value = value;
}

export function valueOf(id) {
  const target = el(id);
  return target ? target.value : "";
}

function slug(value) {
  return String(value || "window")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "") || "window";
}

function displayZone(zone) {
  const aliases = { z1: "hero", z2: "top-right", z3: "bottom-left", z4: "bottom-right" };
  return ZONES.includes(zone) ? zone : aliases[zone] || "side";
}
