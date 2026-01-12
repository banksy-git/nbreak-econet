/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{svelte,js,ts}'],
  theme: {
    extend: {},
  },
  plugins: [],
  safelist: [
    "icon-[mdi--wifi]",
    "icon-[mdi--access-point]",
    "icon-[mdi--console]",
    "icon-[gridicons--stats-up]",
    "icon-[lucide--network]",
    "icon-[hugeicons--gears]",
    "icon-[meteor-icons--wave-square]",
    "icon-[mdi--satellite-uplink]",
  ]
};