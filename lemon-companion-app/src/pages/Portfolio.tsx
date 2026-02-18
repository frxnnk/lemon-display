import { useState, useEffect } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import { useStore } from '../store/useStore'
import NavBar from '../components/NavBar'

const ASSETS = ['BTC', 'ETH', 'SOL', 'USDT', 'USDC']

interface HoldingRow {
  asset: string
  amount: number
}

function Portfolio() {
  const { deviceId } = useParams<{ deviceId: string }>()
  const navigate = useNavigate()
  const { user } = useStore()
  const [holdings, setHoldings] = useState<HoldingRow[]>(
    ASSETS.map((a) => ({ asset: a, amount: 0 }))
  )
  const [saving, setSaving] = useState(false)
  const [saved, setSaved] = useState(false)

  useEffect(() => {
    if (!user?.id) return

    const fetchHoldings = async () => {
      const { data } = await supabase
        .from('portfolio_holdings')
        .select('asset, amount')
        .eq('user_id', user.id)

      if (data) {
        setHoldings(
          ASSETS.map((a) => {
            const found = data.find((h) => h.asset === a)
            return { asset: a, amount: found?.amount ?? 0 }
          })
        )
      }
    }
    fetchHoldings()
  }, [user?.id])

  const updateAmount = (asset: string, value: string) => {
    const num = parseFloat(value) || 0
    setHoldings((prev) =>
      prev.map((h) => (h.asset === asset ? { ...h, amount: num } : h))
    )
    setSaved(false)
  }

  const handleSave = async () => {
    if (!user?.id || !deviceId) return
    setSaving(true)

    try {
      await supabase.functions.invoke('sync-portfolio', {
        body: {
          userId: user.id,
          deviceId,
          holdings: holdings.filter((h) => h.amount > 0),
        },
      })
      setSaved(true)
    } catch (err) {
      console.error('Failed to sync portfolio:', err)
    } finally {
      setSaving(false)
    }
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
        <h1 className="text-xl font-bold flex-1">Portfolio</h1>
      </div>

      <p className="px-4 text-gray-400 text-sm mb-4">
        Ingresa tus tenencias para mostrarlas en el display
      </p>

      {/* Asset rows */}
      <div className="px-4 space-y-3">
        {holdings.map((h) => (
          <div
            key={h.asset}
            className="bg-lemon-card border border-lemon-border rounded-2xl p-4
                       flex items-center justify-between"
          >
            <div className="flex items-center gap-3">
              <span className="w-10 h-10 rounded-full bg-lemon-surface flex items-center justify-center
                              text-sm font-bold text-lemon-green">
                {h.asset.slice(0, 2)}
              </span>
              <span className="font-medium">{h.asset}</span>
            </div>
            <input
              type="number"
              step="any"
              min={0}
              value={h.amount || ''}
              onChange={(e) => updateAmount(h.asset, e.target.value)}
              placeholder="0.00"
              className="w-32 text-right bg-lemon-surface border border-lemon-border rounded-xl
                         px-3 py-2 text-white focus:outline-none focus:border-lemon-green
                         placeholder-gray-600"
            />
          </div>
        ))}
      </div>

      {/* Save button */}
      <div className="px-4 mt-6">
        <button
          onClick={handleSave}
          disabled={saving}
          className={`w-full py-4 font-semibold rounded-2xl active:scale-95 transition-all
            ${saved
              ? 'bg-lemon-green/20 text-lemon-green border border-lemon-green/30'
              : 'bg-lemon-green text-black'
            } disabled:opacity-50`}
        >
          {saving ? 'Guardando...' : saved ? 'Guardado' : 'Guardar portfolio'}
        </button>
      </div>

      <NavBar />
    </div>
  )
}

export default Portfolio
