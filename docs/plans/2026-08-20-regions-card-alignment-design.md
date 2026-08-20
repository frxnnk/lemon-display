# Regions Card Alignment Design

## Goal

Give the four Regions cards one consistent visual grid instead of positioning
their flags separately after the generic card renderer.

## Layout

- Country label: left aligned at 16 px from the card edge.
- Value: left aligned on the central row, with the same baseline in every card.
- Flag: right aligned and vertically centered on the value row.
- Currency: right aligned at the bottom.
- Loading pulse: uses the same origin as the value.

A dedicated `drawRegionCard` renderer is justified by the four real country
cards. It owns the complete card layout and selects the appropriate existing
flag drawing function. No data-fetching or navigation behavior changes.

## Verification

A contract test requires all four cards to use the renderer and rejects the old
detached flag calls. The complete suite and both firmware environments must
remain green before publishing OTA.
