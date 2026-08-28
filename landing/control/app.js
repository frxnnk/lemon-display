const controls = [...document.querySelectorAll("[data-panel]")];
const panels = [...document.querySelectorAll(".panel")];
const dialog = document.querySelector("#action-dialog");
const dialogForm = document.querySelector("#dialog-form");
const dialogTitle = document.querySelector("#dialog-title");
const dialogKicker = document.querySelector("#dialog-kicker");
const dialogContent = document.querySelector("#dialog-content");
const dialogSubmit = document.querySelector("#dialog-submit");
const toastRegion = document.querySelector("#toast-region");
const fleetSearch = document.querySelector("#fleet-search");
const groupFilters = [...document.querySelectorAll("[data-group]")].filter((item) => item.tagName === "BUTTON");
let activeGroup = "all";
let rolloutStage = 0;
let deviceSequence = 151;

const toolDetails = {
    firmware: ["Firmware base", "Base mantenida", "Pantalla, touch, Wi-Fi, caché, operación sin conexión, recuperación y actualización."],
    sdk: ["SDK y plantillas", "8 módulos documentados", "Componentes, contratos de datos y ejemplos para crear experiencias compatibles."],
    simulator: ["Simulador", "Web + entorno local", "Vista previa de contenido, estados y navegación antes de pasar al hardware real."],
    build: ["Compilación", "Compatible con 150/150", "Validaciones técnicas, firma y empaquetado de una versión publicable."],
};

const experienceDetails = {
    Lemon: ["Principal", "96 dispositivos", "Versión 2.4.1", "Producción"],
    "USD₮": ["Activa", "37 dispositivos", "Versión 1.3.0", "Producción"],
    Solana: ["En revisión", "17 dispositivos", "Versión 0.8.2", "Prueba"],
};

function showPanel(id) {
    controls.forEach((control) => {
        const selected = control.dataset.panel === id;
        control.classList.toggle("active", selected);
        control.setAttribute("aria-pressed", String(selected));
    });
    panels.forEach((panel) => panel.classList.toggle("active", panel.id === id));
    history.replaceState(null, "", `#${id}`);
    window.scrollTo({ top: 0, behavior: "auto" });
    document.querySelector(`#${id} h1`)?.focus({ preventScroll: true });
}

function showToast(message) {
    const toast = document.createElement("div");
    toast.className = "toast";
    toast.textContent = message;
    toastRegion.append(toast);
    window.setTimeout(() => toast.remove(), 3200);
}

function field(label, name, value = "", type = "text") {
    return `<label>${label}<input name="${name}" type="${type}" value="${value}" required></label>`;
}

function openDialog(mode, payload = "") {
    dialog.dataset.mode = mode;
    dialog.dataset.payload = payload;
    dialogSubmit.disabled = false;
    dialogSubmit.textContent = "Confirmar";
    dialogKicker.textContent = "LEMON BOX CONTROL";

    if (mode === "register-device") {
        dialogKicker.textContent = "FLOTA / INVENTARIO";
        dialogTitle.textContent = "Registrar equipo";
        dialogContent.innerHTML = `${field("Nombre del equipo", "name", "Lemon Box nueva")}
            <label>Grupo<select name="group"><option value="oficina">Oficina central</option><option value="embajadores">Embajadores</option><option value="partners">Partners</option></select></label>`;
        dialogSubmit.textContent = "Registrar";
    }

    if (mode === "upload-version") {
        dialogKicker.textContent = "SOFTWARE / PUBLICACIÓN";
        dialogTitle.textContent = "Cargar versión";
        dialogContent.innerHTML = `${field("Número de versión", "version", "2.5.1")}
            <label>Notas de la versión<textarea name="notes" required>Mejoras de estabilidad y reconexión.</textarea></label>
            <div class="dialog-detail"><div><span>Firma</span><strong>Requerida</strong></div><div><span>Destino inicial</span><strong>Grupo de prueba</strong></div></div>`;
        dialogSubmit.textContent = "Validar carga";
    }

    if (mode === "new-experience") {
        dialogKicker.textContent = "DEVELOPER KIT";
        dialogTitle.textContent = "Nueva experiencia";
        dialogContent.innerHTML = `${field("Nombre", "name", "Nueva experiencia")}
            <label>Base<select name="base"><option>Lemon principal</option><option>Firmware base limpio</option></select></label>
            <div class="dialog-detail"><div><span>Estado inicial</span><strong>Borrador</strong></div><div><span>Publicación</span><strong>Requiere aprobación</strong></div></div>`;
        dialogSubmit.textContent = "Crear borrador";
    }

    if (mode === "experience") {
        const details = experienceDetails[payload] || ["Borrador", "0 dispositivos", "Sin versión", "Desarrollo"];
        dialogKicker.textContent = "EXPERIENCIA / CONFIGURACIÓN";
        dialogTitle.textContent = payload;
        dialogContent.innerHTML = `<div class="dialog-detail"><div><span>Estado</span><strong>${details[0]}</strong></div><div><span>Asignación</span><strong>${details[1]}</strong></div><div><span>Firmware</span><strong>${details[2]}</strong></div><div><span>Canal</span><strong>${details[3]}</strong></div></div>
            <label>Grupo de publicación<select name="channel"><option>Sin cambios</option><option>Prueba</option><option>Producción</option></select></label>`;
        dialogSubmit.textContent = "Guardar";
    }

    if (mode === "tool") {
        const [title, status, description] = toolDetails[payload];
        dialogKicker.textContent = "DEVELOPER KIT / COMPONENTE";
        dialogTitle.textContent = title;
        dialogContent.innerHTML = `<div class="dialog-detail"><div><span>Estado</span><strong>${status}</strong></div><div><span>Acceso</span><strong>Equipo de desarrollo</strong></div></div><p>${description}</p><p>Incluye documentación y flujo de validación antes de habilitar una publicación.</p>`;
        dialogSubmit.textContent = "Entendido";
    }

    if (mode === "restore-version") {
        dialogKicker.textContent = "SOFTWARE / RESTAURACIÓN";
        dialogTitle.textContent = "Restaurar versión anterior";
        dialogContent.innerHTML = `<div class="dialog-detail"><div><span>Versión actual</span><strong>2.4.1</strong></div><div><span>Versión de destino</span><strong>2.4.0</strong></div><div><span>Grupo</span><strong>Lemon principal</strong></div><div><span>Equipos</span><strong>96</strong></div></div><p>La restauración se inicia primero sobre 5 equipos y requiere aprobación antes de ampliarse.</p>`;
        dialogSubmit.textContent = "Iniciar restauración";
    }

    if (mode === "account") {
        dialogKicker.textContent = "ACCESO / PERMISOS";
        dialogTitle.textContent = "Equipo Lemon";
        dialogContent.innerHTML = `<div class="dialog-detail"><div><span>Rol</span><strong>Administrador</strong></div><div><span>Alcance</span><strong>Toda la flota</strong></div><div><span>Puede preparar</span><strong>Sí</strong></div><div><span>Puede aprobar</span><strong>Sí</strong></div></div><p>Las publicaciones y cambios de configuración quedan registrados con responsable y horario.</p>`;
        dialogSubmit.textContent = "Cerrar";
    }

    dialog.showModal();
}

function filterFleet() {
    const query = fleetSearch.value.trim().toLocaleLowerCase("es");
    let visible = 0;
    document.querySelectorAll("[data-device-row]").forEach((row) => {
        const matchesGroup = activeGroup === "all" || row.dataset.group === activeGroup;
        const matchesQuery = !query || row.textContent.toLocaleLowerCase("es").includes(query);
        row.hidden = !(matchesGroup && matchesQuery);
        if (!row.hidden) visible += 1;
    });
    document.querySelector("#fleet-empty").hidden = visible > 0;
}

function addDevice(formData) {
    const groupLabels = { oficina: "Oficina central", embajadores: "Embajadores", partners: "Partners" };
    const name = formData.get("name").trim();
    const group = formData.get("group");
    const row = document.createElement("div");
    row.dataset.deviceRow = "";
    row.dataset.group = group;
    const identity = document.createElement("span");
    identity.innerHTML = `<i class="online"></i><b></b><small>LB-${String(deviceSequence).padStart(5, "0")}</small>`;
    identity.querySelector("b").textContent = name;
    row.append(identity);
    [groupLabels[group], "2.4.1", "Ahora"].forEach((value) => {
        const cell = document.createElement("span");
        cell.textContent = value;
        row.append(cell);
    });
    const status = document.createElement("em");
    status.textContent = "Operativo";
    row.append(status);
    document.querySelector("#fleet-table").insertBefore(row, document.querySelector("#fleet-empty"));
    deviceSequence += 1;
    document.querySelector(".metrics article:first-child strong").textContent = String(deviceSequence - 1);
    filterFleet();
    showToast(`${name} se registró en ${groupLabels[group]}.`);
}

function uploadVersion(formData) {
    const version = formData.get("version").trim().replace(/^v/i, "");
    document.querySelector("#release-version").textContent = `Versión ${version}`;
    document.querySelector("#release-notes").textContent = formData.get("notes").trim();
    document.querySelector("#release-status").textContent = "VALIDADA · LISTA PARA PRUEBA";
    rolloutStage = 0;
    resetRollout();
    showToast(`Versión ${version} validada para el grupo de prueba.`);
}

function resetRollout() {
    const stages = [...document.querySelectorAll(".rollout article")];
    stages.forEach((stage, index) => {
        stage.classList.toggle("active", index === 0);
        stage.classList.remove("done");
    });
    const button = document.querySelector('[data-action="start-rollout"]');
    button.disabled = false;
    button.textContent = "Iniciar prueba";
    document.querySelector("#rollout-caption").textContent = "Prueba gradual con observación";
}

function advanceRollout() {
    const stages = [...document.querySelectorAll(".rollout article")];
    rolloutStage += 1;
    stages.forEach((stage, index) => {
        stage.classList.toggle("done", index < rolloutStage);
        stage.classList.toggle("active", index === rolloutStage && rolloutStage < stages.length);
    });
    const button = document.querySelector('[data-action="start-rollout"]');
    const actions = ["Iniciar prueba", "Ampliar a 25", "Publicar en 150", "Publicación completa"];
    const captions = ["Prueba gradual con observación", "5 equipos actualizados · sin incidentes", "25 equipos actualizados · sin incidentes", "150 equipos actualizados"];
    button.textContent = actions[Math.min(rolloutStage, 3)];
    document.querySelector("#rollout-caption").textContent = captions[Math.min(rolloutStage, 3)];
    if (rolloutStage >= 3) {
        button.disabled = true;
        document.querySelector("#release-status").textContent = "PUBLICADA EN PRODUCCIÓN";
    }
    showToast(captions[Math.min(rolloutStage, 3)]);
}

function createExperience(formData) {
    const name = formData.get("name").trim();
    experienceDetails[name] = ["Borrador", "0 dispositivos", "Sin versión", "Desarrollo"];
    const card = document.createElement("article");
    card.className = "draft-card";
    card.innerHTML = `<span class="partner-mark generic"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 3 1.7 4.8 4.8 1.7-4.8 1.7L12 16l-1.7-4.8-4.8-1.7 4.8-1.7L12 3Z"/><path d="m18.5 15 .8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8.8-2.2Z"/></svg></span><em>BORRADOR</em><h2></h2><p>Creada desde ${formData.get("base")}.</p><div><b>0 <small>dispositivos</small></b><b>0 <small>pantallas</small></b></div><button data-experience="">Ver configuración ↗</button>`;
    card.querySelector("h2").textContent = name;
    card.querySelector("button").dataset.experience = name;
    document.querySelector("#experience-grid").append(card);
    showToast(`${name} se creó como borrador.`);
}

controls.forEach((control) => control.addEventListener("click", () => showPanel(control.dataset.panel)));
document.querySelectorAll("[data-open]").forEach((control) => control.addEventListener("click", () => showPanel(control.dataset.open)));
fleetSearch.addEventListener("input", filterFleet);
groupFilters.forEach((button) => button.addEventListener("click", () => {
    activeGroup = button.dataset.group;
    groupFilters.forEach((item) => item.classList.toggle("active", item === button));
    filterFleet();
}));

document.addEventListener("click", (event) => {
    const action = event.target.closest("[data-action]")?.dataset.action;
    if (["register-device", "upload-version", "new-experience", "restore-version", "account"].includes(action)) openDialog(action);
    if (action === "start-rollout") advanceRollout();
    const experience = event.target.closest("[data-experience]")?.dataset.experience;
    if (experience) openDialog("experience", experience);
    const tool = event.target.closest("[data-tool]")?.dataset.tool;
    if (tool) openDialog("tool", tool);
});

dialogForm.addEventListener("submit", (event) => {
    if (event.submitter?.value === "cancel") return;
    event.preventDefault();
    const formData = new FormData(dialogForm);
    const mode = dialog.dataset.mode;
    if (mode === "register-device") addDevice(formData);
    if (mode === "upload-version") uploadVersion(formData);
    if (mode === "new-experience") createExperience(formData);
    if (mode === "experience") showToast(`${dialog.dataset.payload}: configuración guardada.`);
    if (mode === "restore-version") {
        document.querySelector("#release-status").textContent = "RESTAURACIÓN EN PRUEBA";
        showToast("Restauración iniciada sobre 5 equipos.");
    }
    dialog.close();
});

const initialPanel = location.hash.slice(1);
if (panels.some((panel) => panel.id === initialPanel)) {
    showPanel(initialPanel);
    requestAnimationFrame(() => requestAnimationFrame(() => window.scrollTo({ top: 0, behavior: "auto" })));
}
