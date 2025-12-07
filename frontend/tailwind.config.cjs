/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./index.html', './src/**/*.{ts,tsx,js,jsx}'],
  theme: {
    extend: {
      colors: {
        bg: '#0b0f1a',
        panel: '#11182a',
        border: '#1f2b44',
        accent: '#5be6ff',
        accent2: '#8cf59b',
        text: '#eef2ff',
        muted: '#9fb2cc',
      },
      fontFamily: {
        sans: ['Inter', 'Segoe UI', 'system-ui', '-apple-system', 'sans-serif'],
      },
    },
  },
  plugins: [],
};
