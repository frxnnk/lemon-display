# Tareas completas Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Construir un gestor local de tareas con subtareas, texto completo, descripción, vencimientos y recordatorios autónomos en la web móvil y la pantalla Ferced.

**Architecture:** Separar el modelo de dominio del almacenamiento y de las dos interfaces. El modelo usa IDs estables y reglas testeables; el almacenamiento migra la lista NVS v1 a un archivo binario versionado y atómico en LittleFS; la web y la UI táctil consumen la misma API del store. El loop del firmware evalúa recordatorios con NTP y presenta una tarjeta local con audio.

**Tech Stack:** C++17/Arduino ESP32-S3, LittleFS, Preferences/NVS para migración, ESPAsyncWebServer, ArduinoJson 7, LovyanGFX/SDL, HTML/CSS/JavaScript embebido, Python unittest para pruebas estáticas y g++ host para el modelo.

---

### Task 1: Modelo jerárquico puro y reglas

**Files:**
- Create: `src/todo_model.h`
- Create: `src/todo_model.cpp`
- Create: `tools/test_todo_model.cpp`
- Create: `tools/test_todo_model.py`

**Steps:**

1. Escribir pruebas host para IDs estables, altas, edición, un nivel de subtareas, orden, cascada de completado, reapertura, vencimientos y posposición.
2. Ejecutar `python tools/test_todo_model.py` y verificar que falla porque el modelo no existe.
3. Implementar estructuras de tamaño acotado y funciones sin dependencias Arduino.
4. Ejecutar el test y esperar `OK`.
5. Commit: `feat: agregar modelo completo de tareas`.

### Task 2: Persistencia versionada y migración segura

**Files:**
- Modify: `src/todo_store.h`
- Modify: `src/todo_store.cpp`
- Modify: `sim/src/sim_todo.cpp`
- Modify: `platformio.ini`
- Create: `tools/test_todo_persistence.py`

**Steps:**

1. Escribir pruebas estáticas para versión, archivo temporal, rename atómico y migración de `ferced_todo` v1.
2. Ejecutarlas y comprobar el fallo inicial.
3. Montar LittleFS sobre la partición existente, guardar `todo-v2.bin.tmp`, renombrar sólo tras validar tamaño/checksum y migrar una sola vez desde NVS.
4. Adaptar el simulador al nuevo contrato y fixture.
5. Ejecutar pruebas del modelo/persistencia y compilar el simulador.
6. Commit: `feat: persistir y migrar tareas completas`.

### Task 3: API por identificadores

**Files:**
- Modify: `src/web_server.cpp`
- Create: `tools/test_todo_api_contract.py`

**Steps:**

1. Definir en tests el JSON versionado y los endpoints por ID.
2. Implementar listado, alta/edición/borrado/reordenamiento de tareas y subtareas, toggle, completar, posponer y errores JSON.
3. Mantener `X-Ferced` en toda mutación y validar límites/fechas en firmware.
4. Ejecutar `python tools/test_todo_api_contract.py` y el build firmware.
5. Commit: `feat: ampliar API local de tareas`.

### Task 4: Web móvil completa

**Files:**
- Modify: `src/web_server.cpp`
- Create: `tools/extract_todo_page.py`
- Create: `tools/test_todo_page.py`

**Steps:**

1. Crear extractor con mock de API y tests para controles, filtros, formulario, accesibilidad y ausencia de dependencias externas.
2. Rediseñar la página con resumen, filtros, tarjetas colapsables, editor de título/descripción/fecha/recordatorio, subtareas y reordenamiento.
3. Evitar que el polling reemplace el formulario activo y mostrar carga/guardado/error/deshacer.
4. Ejecutar tests y abrir la página extraída en viewport móvil para captura visual.
5. Commit: `feat: rediseñar gestor web de tareas`.

### Task 5: Lista táctil sin truncamiento

**Files:**
- Modify: `src/ui_todo.h`
- Modify: `src/ui_todo.cpp`
- Modify: `src/ferced_main.cpp`
- Modify: `sim/src/sim_main.cpp`
- Modify: `sim/shot.ps1`

**Steps:**

1. Agregar fixture con títulos largos, fechas y subtareas.
2. Implementar wrapping por ancho medido, tarjetas de alto variable, expansión, progreso y scroll vertical.
3. Separar zonas táctiles de checkbox, chevron y cuerpo; conservar swipes horizontales de apps.
4. Capturar lista colapsada, expandida y desplazada; revisar que no aparezca `...`.
5. Commit: `feat: mostrar tareas completas en la caja`.

### Task 6: Vista de detalle

**Files:**
- Modify: `src/ui_todo.h`
- Modify: `src/ui_todo.cpp`
- Modify: `src/ferced_main.cpp`
- Modify: `sim/src/sim_main.cpp`
- Modify: `sim/shot.ps1`

**Steps:**

1. Implementar estado lista/detalle y navegación de vuelta.
2. Dibujar título y descripción completos con scroll, metadatos y subtareas accionables.
3. Agregar capturas de detalle corto, largo y sin descripción.
4. Compilar simulador y verificar visualmente las tres.
5. Commit: `feat: agregar detalle táctil de tarea`.

### Task 7: Motor y tarjeta de recordatorios

**Files:**
- Create: `src/todo_reminder.h`
- Create: `src/todo_reminder.cpp`
- Modify: `src/ferced_main.cpp`
- Modify: `src/audio_manager.cpp`
- Modify: `src/ui_todo.h`
- Modify: `src/ui_todo.cpp`
- Modify: `sim/src/sim_main.cpp`
- Modify: `sim/shot.ps1`
- Create: `tools/test_todo_reminder.cpp`

**Steps:**

1. Probar que sólo dispara con reloj válido, una sola vez y que posponer mueve el epoch diez minutos.
2. Implementar evaluación en el loop sin escrituras repetidas.
3. Inicializar audio y reproducir una alerta breve al disparar.
4. Implementar tarjeta con Completar, Posponer 10 min y Abrir tarea.
5. Capturar la tarjeta y verificar las zonas táctiles.
6. Commit: `feat: agregar recordatorios autónomos`.

### Task 8: Verificación integral y documentación

**Files:**
- Modify: `docs/HANDOFF.md`
- Modify: `docs/HANDOFF-UI.md`

**Steps:**

1. Ejecutar todos los tests nuevos y existentes relevantes.
2. Ejecutar `sim/build.ps1`, generar todas las capturas y revisarlas.
3. Ejecutar `python -m platformio run -e ferced_display` y `python -m platformio run -e ferced_display_vps`.
4. Comparar flash/RAM con el baseline y ajustar límites si hace falta.
5. Documentar formato, migración, API, gestos, recordatorios y procedimiento de prueba en hardware.
6. Verificar `git diff --check` y árbol limpio.
7. Commit: `docs: actualizar traspaso de tareas completas`.

