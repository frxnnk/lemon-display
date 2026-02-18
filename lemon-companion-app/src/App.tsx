import { Routes, Route } from 'react-router-dom'
import Landing from './pages/Landing'
import Pairing from './pages/Pairing'
import Dashboard from './pages/Dashboard'
import Config from './pages/Config'
import Portfolio from './pages/Portfolio'
import Alerts from './pages/Alerts'

function App() {
  return (
    <div className="min-h-screen bg-lemon-bg text-white">
      <Routes>
        <Route path="/" element={<Landing />} />
        <Route path="/pairing" element={<Pairing />} />
        <Route path="/dashboard" element={<Dashboard />} />
        <Route path="/config/:deviceId" element={<Config />} />
        <Route path="/portfolio/:deviceId" element={<Portfolio />} />
        <Route path="/alerts/:deviceId" element={<Alerts />} />
      </Routes>
    </div>
  )
}

export default App
