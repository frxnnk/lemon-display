import { useNavigate } from 'react-router-dom'
import { useStore } from '../store/useStore'
import NavBar from '../components/NavBar'

function Dashboard() {
  const navigate = useNavigate()
  const { devices, user } = useStore()

  const formatLastSeen = (iso: string) => {
    const d = new Date(iso)
    const now = new Date()
    const diffMs = now.getTime() - d.getTime()
    const diffMin = Math.floor(diffMs / 60000)

    if (diffMin < 1) return 'Ahora'
    if (diffMin < 60) return `Hace ${diffMin}m`
    const diffH = Math.floor(diffMin / 60)
    if (diffH < 24) return `Hace ${diffH}h`
    return d.toLocaleDateString('es-AR')
  }

  return (
    <div className="min-h-screen pb-20">
      {/* Header */}
      <div className="safe-top px-4 pt-4 pb-2">
        <p className="text-gray-400 text-sm">
          Hola, {user?.displayName || user?.lemonTag || 'Usuario'}
        </p>
        <h1 className="text-xl font-bold">Mis dispositivos</h1>
      </div>

      {/* Devices list */}
      <div className="px-4 space-y-3 mt-4">
        {devices.length === 0 ? (
          <div className="text-center py-12">
            <p className="text-gray-500 mb-4">No tienes dispositivos vinculados</p>
            <button
              onClick={() => navigate('/pairing')}
              className="py-3 px-6 bg-lemon-green text-black font-semibold rounded-xl"
            >
              Vincular dispositivo
            </button>
          </div>
        ) : (
          devices.map((device) => (
            <button
              key={device.id}
              onClick={() => navigate(`/config/${device.id}`)}
              className="w-full bg-lemon-card border border-lemon-border rounded-2xl p-4
                         text-left active:scale-[0.98] transition-transform"
            >
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <span
                    className={`w-2.5 h-2.5 rounded-full ${
                      device.isOnline ? 'bg-lemon-green' : 'bg-negative'
                    }`}
                  />
                  <span className="font-semibold text-lg">
                    {device.deviceCode}
                  </span>
                </div>
                <svg className="w-5 h-5 text-gray-500" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 5l7 7-7 7" />
                </svg>
              </div>

              <div className="flex items-center justify-between text-sm text-gray-400">
                <span>FW {device.firmwareVer}</span>
                <span>{formatLastSeen(device.lastSeen)}</span>
              </div>
            </button>
          ))
        )}
      </div>

      {/* Add device FAB */}
      {devices.length > 0 && (
        <div className="fixed bottom-24 right-4">
          <button
            onClick={() => navigate('/pairing')}
            className="w-14 h-14 bg-lemon-green rounded-full flex items-center justify-center
                       shadow-lg shadow-lemon-green/20 active:scale-90 transition-transform"
          >
            <svg className="w-7 h-7 text-black" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2.5} d="M12 4v16m8-8H4" />
            </svg>
          </button>
        </div>
      )}

      <NavBar />
    </div>
  )
}

export default Dashboard
