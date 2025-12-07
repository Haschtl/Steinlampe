import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './index.css';
import { I18nProvider } from './i18n';
import { ConnectionProvider } from './context/connection';

// Set theme before React renders to avoid flicker
(() => {
  const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  const fallback = prefersDark ? 'fancy' : 'fancy-light';
  const theme = localStorage.getItem('ql-theme') || fallback;
  document.body.setAttribute('data-theme', theme);
})();

ReactDOM.createRoot(document.getElementById('root') as HTMLElement).render(
  <React.StrictMode>
    <I18nProvider>
      <ConnectionProvider>
        <App />
      </ConnectionProvider>
    </I18nProvider>
  </React.StrictMode>,
);

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('./sw.js').catch((err) => {
      console.warn('SW registration failed', err);
    });
  });
}
