// Collect glyphs for the short almanac strings shown on the device.
// Keep a bounded year range so firmware fonts remain a reproducible subset.
import { writeFileSync } from 'node:fs';
import { dailyCalendar } from '../companion/calendar.js';

const chars = new Set();
for (let year = 2024; year <= 2035; year++) {
  const day = new Date(Date.UTC(year, 0, 1));
  while (day.getUTCFullYear() === year) {
    const date = day.toISOString().slice(0, 10);
    const info = dailyCalendar(date);
    for (const char of `${info.lunarDate}${info.yi.slice(0, 2).join('·')}${info.ji.slice(0, 2).join('·')}`)
      chars.add(char);
    day.setUTCDate(day.getUTCDate() + 1);
  }
}
writeFileSync(new URL('../assets/fonts/almanac-glyphs.txt', import.meta.url),
  [...chars].sort().join('') + '\n');
console.log(`Collected ${chars.size} almanac glyphs`);
