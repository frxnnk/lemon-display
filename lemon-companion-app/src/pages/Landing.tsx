import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { authenticate } from '../lib/lemon-sdk'
import { supabase } from '../lib/supabase'
import { useStore } from '../store/useStore'

function Landing() {
  const navigate = useNavigate()
  const { setUser, setDevices, setLoading, setError } = useStore()
  const [connecting, setConnecting] = useState(false)

  const handleConnect = async () => {
    setConnecting(true)
    setError(null)

    try {
      // 1. Authenticate with Lemon SDK (SIWE)
      const siwe = await authenticate()

      // 2. Upsert user in Supabase
      const { data: user, error: userError } = await supabase
        .from('users')
        .upsert(
          {
            wallet_address: siwe.walletAddress,
            lemon_tag: siwe.claims.lemonTag ?? '',
            display_name: siwe.claims.name ?? '',
            email: siwe.claims.email ?? '',
          },
          { onConflict: 'wallet_address' }
        )
        .select()
        .single()

      if (userError) throw userError

      setUser({
        id: user.id,
        walletAddress: user.wallet_address,
        lemonTag: user.lemon_tag,
        displayName: user.display_name,
        email: user.email,
      })

      // 3. Fetch user's devices
      const { data: devices } = await supabase
        .from('devices')
        .select('*')
        .eq('user_id', user.id)

      if (devices && devices.length > 0) {
        setDevices(
          devices.map((d) => ({
            id: d.id,
            deviceCode: d.device_code,
            hardwareId: d.hardware_id,
            firmwareVer: d.firmware_ver,
            userId: d.user_id,
            pairedAt: d.paired_at,
            lastSeen: d.last_seen,
            isOnline: d.is_online,
          }))
        )
        navigate('/dashboard')
      } else {
        navigate('/pairing')
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Error de conexion')
    } finally {
      setConnecting(false)
      setLoading(false)
    }
  }

  return (
    <div className="flex flex-col items-center justify-center min-h-screen px-6">
      {/* Logo area */}
      <div className="mb-12 text-center">
        <div className="w-24 h-24 mx-auto mb-6 rounded-full bg-lemon-green/10 flex items-center justify-center">
          <span className="text-5xl font-bold text-lemon-green">L</span>
        </div>
        <h1 className="text-3xl font-bold mb-2">Lemon Display</h1>
        <p className="text-gray-400 text-sm">
          Configura y personaliza tu display desde tu celular
        </p>
      </div>

      {/* CTA */}
      <button
        onClick={handleConnect}
        disabled={connecting}
        className="w-full max-w-xs py-4 px-6 bg-lemon-green text-black font-semibold rounded-2xl
                   active:scale-95 transition-transform disabled:opacity-50 disabled:scale-100"
      >
        {connecting ? (
          <span className="flex items-center justify-center gap-2">
            <svg className="animate-spin h-5 w-5" viewBox="0 0 24 24">
              <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
              <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            Conectando...
          </span>
        ) : (
          'Conectar mi Lemon Display'
        )}
      </button>

      {/* Error display */}
      {useStore.getState().error && (
        <p className="mt-4 text-negative text-sm text-center">
          {useStore.getState().error}
        </p>
      )}

      {/* Footer */}
      <p className="mt-16 text-gray-600 text-xs">
        Lemon Display Companion v1.0
      </p>
    </div>
  )
}

export default Landing
