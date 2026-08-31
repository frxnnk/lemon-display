# USDt UI and Settings Design

## Goal

Make the production USDt firmware easier to read and configure without adding new network dependencies or blocking the touch loop.

## Screen design

Every content screen uses a centered title group composed of the Tether logo, a fixed gap, and the localized title. Networks, Markets, and Regions remove their explanatory footer lines. The bottom navigation keeps five equal hit areas but adds simple vector icons above short localized labels, with the active destination shown in Tether green.

Home keeps the live ARS price and Argentina flag. Its lower area becomes a two-by-two grid: Variation, Yield, PEG, and ARS Spread. The PEG sparkline is removed. Variation keeps the automatic 1H/24H/7D rotation, while a three-segment indicator inside the card makes the active period obvious. Spread reuses the Lemon bid and ask values already fetched by the device.

## System behavior

System becomes a settings screen instead of a dense provider-status table. It shows compact firmware, Wi-Fi, data, and OTA health, followed by three large touch rows: sound, language, and Wi-Fi reconfiguration. Sound controls the existing audio manager and persists through NVS. Language switches the complete USDt runtime between Spanish and English immediately and persists through NVS. Wi-Fi reconfiguration keeps the existing provisioning flow.

Translation remains local to the USDt firmware: small ES/EN string selection helpers and status mapping, not a generic localization framework. Network values and API contracts do not change.

## Error and loading behavior

Existing card loading pulses remain active only for missing values during a fetch. Cached or valid values stay visible. Provider errors remain represented by neutral placeholders and localized status text. Touch handling for settings is evaluated before normal scene gestures so buttons work independently of network fetch state.

## Verification

Contract tests cover removed copy, centered title geometry, navigation icons, the four-card Home layout, visible variation periods, persisted sound/language controls, translated UI copy, and the `.16` version. The complete Python suite and the `matouch_esp32s3_40_usdt` PlatformIO build must pass before creating an OTA draft.
