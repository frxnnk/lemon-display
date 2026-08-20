const UPSTREAM_URL =
  "https://stablecoins.llama.fi/stablecoins?includePrices=true";

const NETWORKS = [
  ["bsc", "BSC"],
  ["polygon", "Polygon"],
  ["tron", "Tron"],
  ["ethereum", "Ethereum"],
];

function finitePositive(value) {
  return Number.isFinite(value) && value > 0;
}

function buildNetworkPayload(upstream) {
  const tether = upstream?.peggedAssets?.find(
    (asset) => String(asset?.symbol).toUpperCase() === "USDT",
  );
  if (!tether) throw new Error("USDT asset missing from upstream");

  const networks = NETWORKS.map(([id, chain]) => {
    const row = tether.chainCirculating?.[chain];
    const supplyUsd = Number(row?.current?.peggedUSD);
    const previousUsd = Number(row?.circulatingPrevDay?.peggedUSD);
    if (!finitePositive(supplyUsd) || !finitePositive(previousUsd)) {
      throw new Error(`Invalid ${chain} supply from upstream`);
    }
    return {
      id,
      supplyUsd,
      change24h: (supplyUsd / previousUsd - 1) * 100,
    };
  });

  return {
    source: "defillama",
    asOf: new Date().toISOString(),
    networks,
  };
}

async function handler(request, response) {
  if (request.method !== "GET") {
    response.setHeader("Allow", "GET");
    return response.status(405).json({ error: "method_not_allowed" });
  }

  try {
    const upstreamResponse = await fetch(UPSTREAM_URL, {
      headers: { Accept: "application/json" },
      signal: AbortSignal.timeout(8000),
    });
    if (!upstreamResponse.ok) {
      throw new Error(`Upstream HTTP ${upstreamResponse.status}`);
    }
    const payload = buildNetworkPayload(await upstreamResponse.json());
    response.setHeader(
      "Cache-Control",
      "public, s-maxage=300, stale-while-revalidate=86400, stale-if-error=86400",
    );
    return response.status(200).json(payload);
  } catch (error) {
    console.error("usdt-networks proxy failed", error);
    response.setHeader("Cache-Control", "no-store");
    return response.status(502).json({ error: "upstream_unavailable" });
  }
}

module.exports = handler;
module.exports.buildNetworkPayload = buildNetworkPayload;
