/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        'lemon-green': '#00E676',
        'lemon-bg': '#0D0D0D',
        'lemon-surface': '#1A1A1A',
        'lemon-card': '#1E1E1E',
        'lemon-border': '#2A2A2A',
        'solar': '#FF9800',
        'negative': '#FF5252',
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', '-apple-system', 'sans-serif'],
      },
    },
  },
  plugins: [],
}
