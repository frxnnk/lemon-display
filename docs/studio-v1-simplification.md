# Lemon Box Studio v1 simplification

## Product rule

The default Studio UI is for non-technical users. It should optimize for one primary workflow:

1. Open Cajita.
2. Change supported screen settings.
3. Apply supported settings to the physical device over WiFi.

Everything that needs firmware knowledge, agent tooling, recovery knowledge, or more hardware testing stays out of the main UI. Technical mode exposes only operations that are useful and real today.

## Visible by default

- 3D Lemon Box preview.
- 480x480 screen editor.
- Preset settings: theme, layout, watchlist.
- Human-readable device state: connection, WiFi signal, firmware version, and last sync.
- Z2 mode and Apply to device.
- Timeline for the latest jobs.

## Visible in Technical mode

- Build firmware.
- OTA local.
- USB normal flash.
- Device health.
- USB boot verification.

## Hidden from UI but documented as Labs

- Recovery flashing.
- OTA latest controls.
- Release Center download/check flows.
- Key vault.
- MCP/agent draft controls.
- Prompt field.
- Release notes and experience fork metadata.
- Card geometry inputs.
- Weather data sources until firmware support is clear.
- Custom HTTP data sources.

These are not removed because they are useful for development and internal testing, but they are not visible in the product UI until they become understandable and truly device-backed.

## Functional status

- WiFi device registry and pairing: tested with a physical Lemon Box.
- Settings sync over WiFi: tested for brightness and supported settings payload.
- Real data preview: tested for crypto and stocks fallback.
- Data editor: tested for watchlist/ticker updates and preview.
- OTA local: tested.
- OTA health verify: tested, hidden as Labs.
- USB normal flash: tested.
- Full USB recovery: hidden from UI until manually accepted again.
- Arbitrary custom cards on firmware: not available in v1. They remain preview/export only.
- MCP agent tools: draft-only and hidden as Labs. They cannot flash, OTA, pair, or write secrets.

## Next simplification pass

- Replace raw source ids with user-facing names in the card inspector.
- Split device actions into clear buttons if users need separate Apply screen settings and Apply watchlist controls.
- Keep destructive firmware actions out of default UI.
