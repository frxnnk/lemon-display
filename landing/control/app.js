const controls = [...document.querySelectorAll("[data-panel]")];
const panels = [...document.querySelectorAll(".panel")];

function showPanel(id) {
    controls.forEach((control) => {
        const selected = control.dataset.panel === id;
        control.classList.toggle("active", selected);
        control.setAttribute("aria-pressed", String(selected));
    });
    panels.forEach((panel) => panel.classList.toggle("active", panel.id === id));
    history.replaceState(null, "", `#${id}`);
    document.querySelector(`#${id} h1`)?.focus({ preventScroll: true });
}

controls.forEach((control) => control.addEventListener("click", () => showPanel(control.dataset.panel)));
document.querySelectorAll("[data-open]").forEach((control) => control.addEventListener("click", () => showPanel(control.dataset.open)));

const initialPanel = location.hash.slice(1);
if (panels.some((panel) => panel.id === initialPanel)) showPanel(initialPanel);
