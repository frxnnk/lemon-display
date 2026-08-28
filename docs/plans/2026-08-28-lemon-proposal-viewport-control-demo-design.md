# Lemon Box proposal: viewport fit and control demo

## Objective

Keep every commercial section fully readable in one desktop viewport while preserving a detailed, navigable example of the optional operations software.

## Layout decisions

- Sections 02 through 07 use exactly `100dvh` on desktop and return to content-driven height on tablet and mobile.
- Short desktop viewports reduce spacing and type scale without hiding content.
- The manufacturing terms use five columns on desktop so the included firmware program remains visible in the same screen.
- The optional software screen uses two balanced columns. Scope stays on the left. Price, demo entry point and preview form one visual block on the right.
- Anchor navigation aligns each commercial section with the viewport instead of leaving an extra navigation offset.

## Control demo

The detailed demo lives at `/landing/control/` instead of being embedded inside the commercial screen. This keeps the proposal concise and gives Lemon a separate environment to explore:

- Resumen: fleet totals, attention items, editions and recent deployment.
- Dispositivos: inventory, connectivity, installed version and operational state.
- Versiones: staged rollout, active versions and deployment history.
- Partners: independent editions on a shared operational base.

All data is explicitly labeled as illustrative. The demo is a front-end prototype and does not imply connection to a production fleet.

## Verification

- Static regression tests cover the viewport rules, software price order, demo link and demo artifacts.
- Browser validation targets 1280 × 720 as the compact desktop baseline.
- Every proposal section must fit inside its 720 px section bounds.
- Demo navigation must switch panels without console errors.
