export interface User {
  id: string;
  walletAddress: string;
  lemonTag: string;
  displayName: string;
  email: string;
}

export interface Device {
  id: string;
  deviceCode: string;
  hardwareId: string;
  firmwareVer: string;
  userId: string | null;
  pairedAt: string | null;
  lastSeen: string;
  isOnline: boolean;
}

export interface DeviceConfig {
  deviceId: string;
  brightness: number;
  layoutPreset: number;
  selectedPair: number;
  selectedPeriod: number;
  dollarPeriod: number;
  chartStyle: number;
  soundEnabled: boolean;
  alertEnabled: boolean;
  use24h: boolean;
  greetingText: string;
  showPortfolio: boolean;
}

export interface PortfolioHolding {
  id: string;
  userId: string;
  asset: string;
  amount: number;
}

export interface CustomAlert {
  id: string;
  deviceId: string;
  asset: string;
  pair: string;
  condition: 'above' | 'below';
  threshold: number;
  enabled: boolean;
  triggered: boolean;
}
