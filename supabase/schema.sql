-- ══════════════════════════════════════════
--  Lemon Interface — Supabase Schema v1.0
-- ══════════════════════════════════════════
-- Run this in Supabase SQL Editor to bootstrap the database.

-- ── Enable required extensions ──
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ══════════════════════════════════════════
--  TABLES
-- ══════════════════════════════════════════

-- Users (created on first Mini App login via SIWE)
CREATE TABLE IF NOT EXISTS users (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    wallet_address  TEXT UNIQUE NOT NULL,
    lemon_tag       TEXT,
    display_name    TEXT,
    email           TEXT,
    country         TEXT DEFAULT 'AR',
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- Devices (each physical ESP32 box)
CREATE TABLE IF NOT EXISTS devices (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_code     TEXT UNIQUE NOT NULL,           -- 6-char pairing code
    hardware_id     TEXT UNIQUE NOT NULL,            -- ESP32 MAC address
    firmware_ver    TEXT DEFAULT '5.0.0',
    user_id         UUID REFERENCES users(id) ON DELETE SET NULL,
    paired_at       TIMESTAMPTZ,
    last_seen       TIMESTAMPTZ DEFAULT now(),
    is_online       BOOLEAN DEFAULT false,
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- Device config (remote settings, 1 row per device)
CREATE TABLE IF NOT EXISTS device_config (
    device_id       UUID PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
    brightness      SMALLINT DEFAULT 255,
    layout_preset   SMALLINT DEFAULT 0,
    selected_pair   SMALLINT DEFAULT 0,
    selected_period SMALLINT DEFAULT 4,
    dollar_period   SMALLINT DEFAULT 2,
    chart_style     SMALLINT DEFAULT 0,
    sound_enabled   BOOLEAN DEFAULT true,
    alert_enabled   BOOLEAN DEFAULT true,
    use_24h         BOOLEAN DEFAULT true,
    greeting_text   TEXT DEFAULT '',
    show_portfolio  BOOLEAN DEFAULT false,
    updated_at      TIMESTAMPTZ DEFAULT now()
);

-- Portfolio holdings (manual input from Mini App)
CREATE TABLE IF NOT EXISTS portfolio_holdings (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id         UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset           TEXT NOT NULL,                   -- "BTC", "ETH", "SOL", "USDT", "USDC"
    amount          DECIMAL(20,8) DEFAULT 0,
    updated_at      TIMESTAMPTZ DEFAULT now(),
    UNIQUE(user_id, asset)
);

-- Custom alerts (configured from Mini App)
CREATE TABLE IF NOT EXISTS custom_alerts (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id       UUID NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    asset           TEXT DEFAULT 'BTC',
    pair            TEXT DEFAULT 'USD',
    condition       TEXT CHECK (condition IN ('above', 'below')),
    threshold       DECIMAL(20,2) NOT NULL,
    enabled         BOOLEAN DEFAULT true,
    triggered       BOOLEAN DEFAULT false,
    created_at      TIMESTAMPTZ DEFAULT now()
);

-- ══════════════════════════════════════════
--  INDEXES
-- ══════════════════════════════════════════

CREATE INDEX IF NOT EXISTS idx_devices_user_id ON devices(user_id);
CREATE INDEX IF NOT EXISTS idx_devices_hardware_id ON devices(hardware_id);
CREATE INDEX IF NOT EXISTS idx_devices_device_code ON devices(device_code);
CREATE INDEX IF NOT EXISTS idx_portfolio_user_id ON portfolio_holdings(user_id);
CREATE INDEX IF NOT EXISTS idx_alerts_device_id ON custom_alerts(device_id);

-- ══════════════════════════════════════════
--  ROW LEVEL SECURITY (RLS)
-- ══════════════════════════════════════════

ALTER TABLE users ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE device_config ENABLE ROW LEVEL SECURITY;
ALTER TABLE portfolio_holdings ENABLE ROW LEVEL SECURITY;
ALTER TABLE custom_alerts ENABLE ROW LEVEL SECURITY;

-- Users: can read/update own row
CREATE POLICY "Users can view own profile"
    ON users FOR SELECT
    USING (auth.uid() = id);

CREATE POLICY "Users can update own profile"
    ON users FOR UPDATE
    USING (auth.uid() = id);

-- Devices: users see their paired devices
CREATE POLICY "Users can view own devices"
    ON devices FOR SELECT
    USING (user_id = auth.uid());

CREATE POLICY "Users can update own devices"
    ON devices FOR UPDATE
    USING (user_id = auth.uid());

-- Device config: users can manage config for their devices
CREATE POLICY "Users can view own device config"
    ON device_config FOR SELECT
    USING (
        device_id IN (SELECT id FROM devices WHERE user_id = auth.uid())
    );

CREATE POLICY "Users can update own device config"
    ON device_config FOR UPDATE
    USING (
        device_id IN (SELECT id FROM devices WHERE user_id = auth.uid())
    );

CREATE POLICY "Users can insert device config"
    ON device_config FOR INSERT
    WITH CHECK (
        device_id IN (SELECT id FROM devices WHERE user_id = auth.uid())
    );

-- Portfolio: users manage own holdings
CREATE POLICY "Users can view own portfolio"
    ON portfolio_holdings FOR SELECT
    USING (user_id = auth.uid());

CREATE POLICY "Users can manage own portfolio"
    ON portfolio_holdings FOR ALL
    USING (user_id = auth.uid());

-- Alerts: users manage alerts on their devices
CREATE POLICY "Users can view own alerts"
    ON custom_alerts FOR SELECT
    USING (
        device_id IN (SELECT id FROM devices WHERE user_id = auth.uid())
    );

CREATE POLICY "Users can manage own alerts"
    ON custom_alerts FOR ALL
    USING (
        device_id IN (SELECT id FROM devices WHERE user_id = auth.uid())
    );

-- ══════════════════════════════════════════
--  REALTIME
-- ══════════════════════════════════════════
-- Enable Realtime for device_config (ESP32 subscribes to changes)
-- and devices (for pairing detection)

ALTER PUBLICATION supabase_realtime ADD TABLE device_config;
ALTER PUBLICATION supabase_realtime ADD TABLE devices;

-- ══════════════════════════════════════════
--  HELPER FUNCTIONS
-- ══════════════════════════════════════════

-- Generate random 6-char alphanumeric code
CREATE OR REPLACE FUNCTION generate_device_code()
RETURNS TEXT AS $$
DECLARE
    chars TEXT := 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    result TEXT := '';
    i INT;
BEGIN
    FOR i IN 1..6 LOOP
        result := result || substr(chars, floor(random() * length(chars) + 1)::int, 1);
    END LOOP;
    RETURN result;
END;
$$ LANGUAGE plpgsql;

-- Auto-update updated_at timestamp
CREATE OR REPLACE FUNCTION update_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_device_config_updated_at
    BEFORE UPDATE ON device_config
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at();

CREATE TRIGGER set_portfolio_updated_at
    BEFORE UPDATE ON portfolio_holdings
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at();
