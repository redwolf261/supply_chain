/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The C++ server (scx_server) listens on 127.0.0.1:8080. Proxying /api keeps the browser on a single
// origin in both `npm run dev` and `npm run preview`, so no CORS setup is needed for normal use.
const backend = process.env.SCX_BACKEND ?? 'http://127.0.0.1:8080';
const proxy = { '/api': { target: backend, changeOrigin: true } };

export default defineConfig({
  plugins: [react()],
  server: { host: '127.0.0.1', port: 5173, proxy },
  preview: { host: '127.0.0.1', port: 4173, proxy },
  test: { environment: 'node', include: ['src/**/*.test.ts'] },
});
