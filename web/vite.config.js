import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// base './': the build works from any folder (the site embeds it under /silt-app/).
export default defineConfig({
  base: './',
  plugins: [react()],
  worker: { format: 'es' },
  server: { host: '127.0.0.1' },
});
