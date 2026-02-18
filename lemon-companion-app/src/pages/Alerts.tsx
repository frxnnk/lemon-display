import { useState, useEffect } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { CustomAlert } from '../types'
import NavBar from '../components/NavBar'

const ALERT_ASSETS = ['BTC', 'ETH', 'SOL']
const ALERT_PAIRS = ['USD', 'ARS']
const CONDITIONS: Array<{ value: 'above' | 'below'; label: string }> = [
  { value: 'above', label: 'Mayor a' },
  { value: 'below', label: 'Menor a' },
]

function Alerts() {
  const { deviceId } = useParams<{ deviceId: string }>()
  const navigate = useNavigate()
  const [alerts, setAlerts] = useState<CustomAlert[]>([])
  const [showForm, setShowForm] = useState(false)

  // New alert form state
  const [newAsset, setNewAsset] = useState(ALERT_ASSETS[0])
  const [newPair, setNewPair] = useState(ALERT_PAIRS[0])
  const [newCondition, setNewCondition] = useState<'above' | 'below'>('above')
  const [newThreshold, setNewThreshold] = useState('')
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    if (!deviceId) return

    const fetchAlerts = async () => {
      const { data } = await supabase
        .from('custom_alerts')
        .select('*')
        .eq('device_id', deviceId)
        .order('created_at', { ascending: false })

      if (data) {
        setAlerts(
          data.map((a) => ({
            id: a.id,
            deviceId: a.device_id,
            asset: a.asset,
            pair: a.pair,
            condition: a.condition,
            threshold: a.threshold,
            enabled: a.enabled,
            triggered: a.triggered,
          }))
        )
      }
    }
    fetchAlerts()
  }, [deviceId])

  const handleCreate = async () => {
    if (!deviceId || !newThreshold) return
    setSaving(true)

    try {
      const { data, error } = await supabase
        .from('custom_alerts')
        .insert({
          device_id: deviceId,
          asset: newAsset,
          pair: newPair,
          condition: newCondition,
          threshold: parseFloat(newThreshold),
          enabled: true,
          triggered: false,
        })
        .select()
        .single()

      if (error) throw error

      setAlerts((prev) => [
        {
          id: data.id,
          deviceId: data.device_id,
          asset: data.asset,
          pair: data.pair,
          condition: data.condition,
          threshold: data.threshold,
          enabled: data.enabled,
          triggered: data.triggered,
        },
        ...prev,
      ])

      // Reset form
      setShowForm(false)
      setNewThreshold('')
    } catch (err) {
      console.error('Failed to create alert:', err)
    } finally {
      setSaving(false)
    }
  }

  const toggleAlert = async (alertId: string, enabled: boolean) => {
    setAlerts((prev) =>
      prev.map((a) => (a.id === alertId ? { ...a, enabled } : a))
    )

    await supabase
      .from('custom_alerts')
      .update({ enabled })
      .eq('id', alertId)
  }

  const deleteAlert = async (alertId: string) => {
    setAlerts((prev) => prev.filter((a) => a.id !== alertId))

    await supabase
      .from('custom_alerts')
      .delete()
      .eq('id', alertId)
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
        <h1 className="text-xl font-bold flex-1">Alertas</h1>
        <button
          onClick={() => setShowForm(!showForm)}
          className="text-lemon-green text-sm font-medium"
        >
          {showForm ? 'Cancelar' : '+ Nueva'}
        </button>
      </div>

      {/* New alert form */}
      {showForm && (
        <div className="mx-4 mt-4 bg-lemon-card border border-lemon-green/30 rounded-2xl p-4 space-y-3">
          <h3 className="font-medium text-sm text-lemon-green">Nueva alerta</h3>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="text-xs text-gray-400 block mb-1">Activo</label>
              <select
                value={newAsset}
                onChange={(e) => setNewAsset(e.target.value)}
                className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-3 py-2
                           text-white text-sm focus:outline-none focus:border-lemon-green"
              >
                {ALERT_ASSETS.map((a) => (
                  <option key={a} value={a}>{a}</option>
                ))}
              </select>
            </div>
            <div>
              <label className="text-xs text-gray-400 block mb-1">Par</label>
              <select
                value={newPair}
                onChange={(e) => setNewPair(e.target.value)}
                className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-3 py-2
                           text-white text-sm focus:outline-none focus:border-lemon-green"
              >
                {ALERT_PAIRS.map((p) => (
                  <option key={p} value={p}>{p}</option>
                ))}
              </select>
            </div>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="text-xs text-gray-400 block mb-1">Condicion</label>
              <select
                value={newCondition}
                onChange={(e) => setNewCondition(e.target.value as 'above' | 'below')}
                className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-3 py-2
                           text-white text-sm focus:outline-none focus:border-lemon-green"
              >
                {CONDITIONS.map((c) => (
                  <option key={c.value} value={c.value}>{c.label}</option>
                ))}
              </select>
            </div>
            <div>
              <label className="text-xs text-gray-400 block mb-1">Precio</label>
              <input
                type="number"
                step="any"
                value={newThreshold}
                onChange={(e) => setNewThreshold(e.target.value)}
                placeholder="0.00"
                className="w-full bg-lemon-surface border border-lemon-border rounded-xl px-3 py-2
                           text-white text-sm focus:outline-none focus:border-lemon-green
                           placeholder-gray-600"
              />
            </div>
          </div>

          <button
            onClick={handleCreate}
            disabled={!newThreshold || saving}
            className="w-full py-3 bg-lemon-green text-black font-semibold rounded-xl
                       active:scale-95 transition-transform disabled:opacity-30 text-sm"
          >
            {saving ? 'Creando...' : 'Crear alerta'}
          </button>
        </div>
      )}

      {/* Alerts list */}
      <div className="px-4 space-y-3 mt-4">
        {alerts.length === 0 && !showForm ? (
          <div className="text-center py-12">
            <p className="text-gray-500 mb-2">No tienes alertas configuradas</p>
            <p className="text-gray-600 text-sm">
              Crea alertas para recibir notificaciones en tu display
            </p>
          </div>
        ) : (
          alerts.map((alert) => (
            <div
              key={alert.id}
              className={`bg-lemon-card border rounded-2xl p-4 ${
                alert.triggered
                  ? 'border-solar'
                  : alert.enabled
                  ? 'border-lemon-border'
                  : 'border-lemon-border opacity-50'
              }`}
            >
              <div className="flex items-center justify-between mb-2">
                <span className="font-semibold">
                  {alert.asset}/{alert.pair}
                </span>
                <div className="flex items-center gap-3">
                  {/* Toggle */}
                  <button
                    onClick={() => toggleAlert(alert.id, !alert.enabled)}
                    className={`w-10 h-5 rounded-full transition-colors relative ${
                      alert.enabled ? 'bg-lemon-green' : 'bg-lemon-border'
                    }`}
                  >
                    <span
                      className={`absolute top-0.5 w-4 h-4 bg-white rounded-full transition-transform shadow ${
                        alert.enabled ? 'translate-x-[22px]' : 'translate-x-0.5'
                      }`}
                    />
                  </button>
                  {/* Delete */}
                  <button
                    onClick={() => deleteAlert(alert.id)}
                    className="text-negative text-sm"
                  >
                    <svg className="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                        d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                    </svg>
                  </button>
                </div>
              </div>

              <p className="text-sm text-gray-400">
                {alert.condition === 'above' ? 'Mayor a' : 'Menor a'}{' '}
                <span className="text-white font-medium">
                  ${alert.threshold.toLocaleString()}
                </span>
                {alert.triggered && (
                  <span className="ml-2 text-solar text-xs font-medium">DISPARADA</span>
                )}
              </p>
            </div>
          ))
        )}
      </div>

      <NavBar />
    </div>
  )
}

export default Alerts
