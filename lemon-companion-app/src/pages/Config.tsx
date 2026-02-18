import { useState, useEffect } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { DeviceConfig } from '../types'
import NavBar from '../components/NavBar'

const PAIRS = ['USD', 'ETH', 'SOL', 'ARS', 'ORO']
const PERIODS = ['5m', '15m', '1h', '4h', '1d', '1w', '1M', '1Y']
const LAYOUTS = [
  { id: 0, label: 'Estandar' },
  { id: 1, label: 'BTC Focus' },
  { id: 2, label: 'Compacto' },
]

function Config() {
  const { deviceId } = useParams<{ deviceId: string }>()
  const navigate = useNavigate()
  const [config, setConfig] = useState<DeviceConfig | null>(null)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    if (!deviceId) return

    const fetchConfig = async () => {
      const { data } = await supabase
        .from('device_config')
        .select('*')
        .eq('device_id', deviceId)
        .single()

      if (data) {
        setConfig({
          deviceId: data.device_id,
          brightness: data.brightness,
          layoutPreset: data.layout_preset,
          selectedPair: data.selected_pair,
          selectedPeriod: data.selected_period,
          dollarPeriod: data.dollar_period,
          chartStyle: data.chart_style,
          soundEnabled: data.sound_enabled,
          alertEnabled: data.alert_enabled,
          use24h: data.use_24h,
          greetingText: data.greeting_text,
          showPortfolio: data.show_portfolio,
        })
      }
    }
    fetchConfig()
  }, [deviceId])

  const updateField = async <K extends keyof DeviceConfig>(
    field: K,
    value: DeviceConfig[K]
  ) => {
    if (!config || !deviceId) return

    setConfig({ ...config, [field]: value })
    setSaving(true)

    // Map camelCase to snake_case for Supabase
    const snakeMap: Record<string, string> = {
      brightness: 'brightness',
      layoutPreset: 'layout_preset',
      selectedPair: 'selected_pair',
      selectedPeriod: 'selected_period',
      dollarPeriod: 'dollar_period',
      chartStyle: 'chart_style',
      soundEnabled: 'sound_enabled',
      alertEnabled: 'alert_enabled',
      use24h: 'use_24h',
      greetingText: 'greeting_text',
      showPortfolio: 'show_portfolio',
    }

    await supabase
      .from('device_config')
      .update({ [snakeMap[field]]: value, updated_at: new Date().toISOString() })
      .eq('device_id', deviceId)

    setSaving(false)
  }

  if (!config) {
    return (
      <div className="flex items-center justify-center min-h-screen">
        <div className="animate-spin w-8 h-8 border-2 border-lemon-green border-t-transparent rounded-full" />
      </div>
    )
  }

  return (
    <div className="min-h-screen pb-20">
      {/* Header */}
      <div className="safe-top px-4 pt-4 pb-2 flex items-center gap-3">
        <button onClick={() => navigate('/dashboard')} className="p-1 text-gray-400">
          <svg className="w-6 h-6" fill="none" viewBox="0 0 24 24" stroke="currentColor">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 19l-7-7 7-7" />
          </svg>
        </button>
        <h1 className="text-xl font-bold flex-1">Configuracion</h1>
        {saving && (
          <span className="text-xs text-lemon-green">Guardando...</span>
        )}
      </div>

      <div className="px-4 space-y-4 mt-4">
        {/* Brightness */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4">
          <label className="text-sm text-gray-400 block mb-2">
            Brillo ({config.brightness})
          </label>
          <input
            type="range"
            min={0}
            max={255}
            value={config.brightness}
            onChange={(e) => updateField('brightness', Number(e.target.value))}
            className="w-full accent-lemon-green"
          />
        </div>

        {/* Layout selector */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4">
          <label className="text-sm text-gray-400 block mb-3">Layout</label>
          <div className="flex gap-2">
            {LAYOUTS.map((l) => (
              <button
                key={l.id}
                onClick={() => updateField('layoutPreset', l.id)}
                className={`flex-1 py-2 px-3 rounded-xl text-sm font-medium transition-colors ${
                  config.layoutPreset === l.id
                    ? 'bg-lemon-green text-black'
                    : 'bg-lemon-surface text-gray-400'
                }`}
              >
                {l.label}
              </button>
            ))}
          </div>
        </div>

        {/* Pair selector */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4">
          <label className="text-sm text-gray-400 block mb-2">Par BTC</label>
          <select
            value={config.selectedPair}
            onChange={(e) => updateField('selectedPair', Number(e.target.value))}
            className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-4 py-3
                       text-white focus:outline-none focus:border-lemon-green"
          >
            {PAIRS.map((p, i) => (
              <option key={p} value={i}>BTC/{p}</option>
            ))}
          </select>
        </div>

        {/* Period selector */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4">
          <label className="text-sm text-gray-400 block mb-3">Periodo</label>
          <div className="grid grid-cols-4 gap-2">
            {PERIODS.map((p, i) => (
              <button
                key={p}
                onClick={() => updateField('selectedPeriod', i)}
                className={`py-2 rounded-xl text-sm font-medium transition-colors ${
                  config.selectedPeriod === i
                    ? 'bg-lemon-green text-black'
                    : 'bg-lemon-surface text-gray-400'
                }`}
              >
                {p}
              </button>
            ))}
          </div>
        </div>

        {/* Greeting text */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4">
          <label className="text-sm text-gray-400 block mb-2">Mensaje de saludo</label>
          <input
            type="text"
            maxLength={20}
            value={config.greetingText}
            onChange={(e) => updateField('greetingText', e.target.value)}
            placeholder="Hola!"
            className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-4 py-3
                       text-white focus:outline-none focus:border-lemon-green placeholder-gray-600"
          />
        </div>

        {/* Toggles */}
        <div className="bg-lemon-card border border-lemon-border rounded-2xl p-4 space-y-4">
          <Toggle
            label="Sonido"
            checked={config.soundEnabled}
            onChange={(v) => updateField('soundEnabled', v)}
          />
          <Toggle
            label="Alertas"
            checked={config.alertEnabled}
            onChange={(v) => updateField('alertEnabled', v)}
          />
          <Toggle
            label="Formato 24h"
            checked={config.use24h}
            onChange={(v) => updateField('use24h', v)}
          />
          <Toggle
            label="Mostrar portfolio"
            checked={config.showPortfolio}
            onChange={(v) => updateField('showPortfolio', v)}
          />
        </div>
      </div>

      <NavBar />
    </div>
  )
}

function Toggle({
  label,
  checked,
  onChange,
}: {
  label: string
  checked: boolean
  onChange: (v: boolean) => void
}) {
  return (
    <div className="flex items-center justify-between">
      <span className="text-sm">{label}</span>
      <button
        onClick={() => onChange(!checked)}
        className={`w-11 h-6 rounded-full transition-colors relative ${
          checked ? 'bg-lemon-green' : 'bg-lemon-border'
        }`}
      >
        <span
          className={`absolute top-0.5 w-5 h-5 bg-white rounded-full transition-transform shadow ${
            checked ? 'translate-x-[22px]' : 'translate-x-0.5'
          }`}
        />
      </button>
    </div>
  )
}

export default Config
