import { create } from 'zustand'
import type { User, Device } from '../types'

interface AppState {
  // State
  user: User | null;
  devices: Device[];
  activeDevice: string | null;
  loading: boolean;
  error: string | null;

  // Actions
  setUser: (user: User | null) => void;
  addDevice: (device: Device) => void;
  setDevices: (devices: Device[]) => void;
  setActiveDevice: (deviceId: string | null) => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
}

export const useStore = create<AppState>((set) => ({
  // Initial state
  user: null,
  devices: [],
  activeDevice: null,
  loading: false,
  error: null,

  // Actions
  setUser: (user) => set({ user }),
  addDevice: (device) =>
    set((state) => ({ devices: [...state.devices, device] })),
  setDevices: (devices) => set({ devices }),
  setActiveDevice: (deviceId) => set({ activeDevice: deviceId }),
  setLoading: (loading) => set({ loading }),
  setError: (error) => set({ error }),
}))
