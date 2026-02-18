// Mock implementation - replace with actual @lemoncash/mini-app-sdk when available

export interface SIWEResult {
  walletAddress: string;
  claims: {
    name?: string;
    lemonTag?: string;
    email?: string;
  };
}

export function isWebView(): boolean {
  return typeof window !== 'undefined' &&
    (window.navigator.userAgent.includes('LemonCash') ||
     window.parent !== window);
}

export async function authenticate(): Promise<SIWEResult> {
  // In production, this calls @lemoncash/mini-app-sdk authenticate()
  // For development, return mock data
  if (import.meta.env.DEV) {
    return {
      walletAddress: '0xdev1234567890abcdef',
      claims: {
        name: 'Dev User',
        lemonTag: 'devuser',
        email: 'dev@lemon.me',
      },
    };
  }
  throw new Error('Lemon SDK not available outside WebView');
}
