import { useState, useRef, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import { useStore } from '../store/useStore'

const CODE_LENGTH = 6

function Pairing() {
  const navigate = useNavigate()
  const { user, addDevice, setError } = useStore()
  const [code, setCode] = useState<string[]>(Array(CODE_LENGTH).fill(''))
  const [submitting, setSubmitting] = useState(false)
  const [pairError, setPairError] = useState<string | null>(null)
  const inputRefs = useRef<(HTMLInputElement | null)[]>([])

  useEffect(() => {
    // Auto-focus first input
    inputRefs.current[0]?.focus()
  }, [])

  const handleChange = (index: number, value: string) => {
    // Only allow alphanumeric
    const char = value.replace(/[^A-Za-z0-9]/g, '').toUpperCase().slice(-1)
    const newCode = [...code]
    newCode[index] = char
    setCode(newCode)

    // Auto-advance to next input
    if (char && index < CODE_LENGTH - 1) {
      inputRefs.current[index + 1]?.focus()
    }
  }

  const handleKeyDown = (index: number, e: React.KeyboardEvent) => {
    if (e.key === 'Backspace' && !code[index] && index > 0) {
      inputRefs.current[index - 1]?.focus()
    }
  }

  const handlePaste = (e: React.ClipboardEvent) => {
    e.preventDefault()
    const pasted = e.clipboardData
      .getData('text')
      .replace(/[^A-Za-z0-9]/g, '')
      .toUpperCase()
      .slice(0, CODE_LENGTH)

    const newCode = [...code]
    for (let i = 0; i < pasted.length; i++) {
      newCode[i] = pasted[i]
    }
    setCode(newCode)

    // Focus last filled or next empty
    const focusIdx = Math.min(pasted.length, CODE_LENGTH - 1)
    inputRefs.current[focusIdx]?.focus()
  }

  const handleSubmit = async () => {
    const pairingCode = code.join('')
    if (pairingCode.length !== CODE_LENGTH) return

    setSubmitting(true)
    setPairError(null)

    try {
      // Call pair-device edge function
      const { data, error } = await supabase.functions.invoke('pair-device', {
        body: {
          pairingCode,
          userId: user?.id,
        },
      })

      if (error) throw error
      if (data?.error) throw new Error(data.error)

      // Add device to store
      addDevice({
        id: data.device.id,
        deviceCode: data.device.device_code,
        hardwareId: data.device.hardware_id,
        firmwareVer: data.device.firmware_ver,
        userId: data.device.user_id,
        pairedAt: data.device.paired_at,
        lastSeen: data.device.last_seen,
        isOnline: data.device.is_online,
      })

      navigate('/dashboard')
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Error al vincular'
      setPairError(msg)
      setError(msg)
    } finally {
      setSubmitting(false)
    }
  }

  const isComplete = code.every((c) => c !== '')

  return (
    <div className="flex flex-col items-center justify-center min-h-screen px-6">
      {/* Back button */}
      <button
        onClick={() => navigate(-1)}
        className="absolute top-4 left-4 p-2 text-gray-400"
      >
        <svg className="w-6 h-6" fill="none" viewBox="0 0 24 24" stroke="currentColor">
          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 19l-7-7 7-7" />
        </svg>
      </button>

      <h1 className="text-2xl font-bold mb-2">Vincular dispositivo</h1>
      <p className="text-gray-400 text-sm mb-8 text-center">
        Ingresa el codigo de 6 caracteres que aparece en tu Lemon Display
      </p>

      {/* OTP-style inputs */}
      <div className="flex gap-3 mb-8">
        {code.map((char, i) => (
          <input
            key={i}
            ref={(el) => { inputRefs.current[i] = el }}
            type="text"
            inputMode="text"
            maxLength={1}
            value={char}
            onChange={(e) => handleChange(i, e.target.value)}
            onKeyDown={(e) => handleKeyDown(i, e)}
            onPaste={i === 0 ? handlePaste : undefined}
            className="w-12 h-14 text-center text-xl font-bold bg-lemon-surface border-2
                       border-lemon-border rounded-xl focus:border-lemon-green focus:outline-none
                       transition-colors uppercase"
          />
        ))}
      </div>

      {/* Error */}
      {pairError && (
        <p className="mb-4 text-negative text-sm text-center">{pairError}</p>
      )}

      {/* Submit */}
      <button
        onClick={handleSubmit}
        disabled={!isComplete || submitting}
        className="w-full max-w-xs py-4 px-6 bg-lemon-green text-black font-semibold rounded-2xl
                   active:scale-95 transition-transform disabled:opacity-30 disabled:scale-100"
      >
        {submitting ? 'Vinculando...' : 'Vincular'}
      </button>

      <p className="mt-6 text-gray-600 text-xs text-center max-w-xs">
        El codigo aparece en Ajustes &gt; Vinculacion de tu Lemon Display
      </p>
    </div>
  )
}

export default Pairing
