# USDt Supply and Wi-Fi Recovery Design

## Product decision

Replace the Overview `SPREAD ARS` card with `SUPPLY USDt`. The value represents
Tether's global circulating USD supply from DefiLlama, not the sum of the four
chains shown on the Networks screen. Display it with the existing compact USD
supply formatter and use the existing loading pulse while no valid network
snapshot is available.

## Data flow

The existing `/api/usdt-networks` proxy already finds the USDT asset in
DefiLlama. Extend its small response with `totalSupplyUsd` from
`tether.circulating.peggedUSD`. Parse that field into `UsdtNetworkData` while
preserving the four per-chain metrics and their current cache and retry rules.
An invalid or missing total makes the network response invalid, preventing a
partial value from being presented as global supply.

## Wi-Fi recovery

Stored credentials remain eligible for reconnect even when the first 15-second
boot attempt fails. `wifiLoop()` applies the existing exponential backoff from
10 seconds to five minutes without requiring `everConnected`. A failed initial
attempt disconnects without erasing the station configuration. Once connected,
the USDT runtime already restarts its worker, marks data as fetching, and redraws
the loading animation.

## Release and verification

Ship both changes as `5.1.1-usdt.22`. Verify proxy fixture output, parser/UI
contracts, initial Wi-Fi retry behavior, the full Python suite, the production
USDT build, release hashes, the live proxy response, and the GitHub OTA asset.

