// Builds the Morpheus icon atlas: white glyphs on transparent, each at the
// exact pixel size it is drawn, packed into one 256x256 PNG, plus a C header
// with the rectangles. The theme tints them when drawing.
//
//   npm install puppeteer-core && node tools/morpheus/atlas.js OUT_DIR
//
// Needs Google Chrome (macOS path below). Copy OUT_DIR/icons.png to
// assets/morpheus/ and OUT_DIR/mph_icons.h to src/gui/themes/morpheus/.
const puppeteer = require('puppeteer-core');
const fs = require('fs');
const path = require('path');
const OUT = process.argv[2];

const P = {
  disc: '<circle cx="12" cy="12" r="11" fill="#fff"/>',
  cross: '<path d="M7.5 7.5l9 9M16.5 7.5l-9 9" stroke="#fff" stroke-width="2.4" stroke-linecap="round" fill="none"/>',
  circle: '<circle cx="12" cy="12" r="5.5" stroke="#fff" stroke-width="2.4" fill="none"/>',
  triangle: '<path d="M12 6.5l6 10.2H6z" stroke="#fff" stroke-width="2.2" stroke-linejoin="round" fill="none"/>',
  square: '<rect x="7" y="7" width="10" height="10" rx="1" stroke="#fff" stroke-width="2.2" fill="none"/>',
  pc: '<g fill="none" stroke="#fff" stroke-width="1.6"><rect x="3" y="4" width="18" height="12" rx="1.5"/><path d="M9 20h6M12 16v4"/></g>',
  search: '<g fill="none" stroke="#fff" stroke-width="1.6"><circle cx="11" cy="11" r="6"/><path d="M20 20l-4.5-4.5"/></g>',
  plus: '<path d="M12 5v14M5 12h14" fill="none" stroke="#fff" stroke-width="1.6"/>',
  gear: '<g fill="none" stroke="#fff" stroke-width="1.8"><circle cx="12" cy="12" r="3.2"/><path d="M12 2.8v3M12 18.2v3M2.8 12h3M18.2 12h3M5.5 5.5l2.1 2.1M16.4 16.4l2.1 2.1M5.5 18.5l2.1-2.1M16.4 7.6l2.1-2.1"/></g>',
  power: '<g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round"><path d="M12 3.5v8"/><path d="M7.2 6.6a7 7 0 1 0 9.6 0"/></g>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke="#fff" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>',
  chevron: '<path d="M9 5.5l6.5 6.5L9 18.5" fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>',
  sprint: '<g fill="#fff"><circle cx="15" cy="4.5" r="2.2"/><path d="M10.2 8.2l4.6-.9c.6-.1 1.2.2 1.5.7l1.6 2.9 2.6 1.1-.8 1.7-3.2-1.3-1-1.7-1.4 3.3 2.6 2.4-.9 6h-2l.7-4.9-2.9-2.3-1.6 3.7-4.6 1.6-.6-1.8 3.8-1.4 2.5-6.2-1.7.3-1.6 2.7-1.7-1z"/></g>',
  moon: '<path d="M15.5 3.2a9 9 0 1 0 5.3 13.6A7.4 7.4 0 0 1 15.5 3.2z" fill="#fff"/>',
};
const WIFI = {
  wifi_dot: '<path d="M10 14.5l2.4-2.9a3.6 3.6 0 0 0-4.8 0z" fill="#fff"/>',
  wifi_mid: '<path d="M4.8 8.4l1.6 1.9a5.6 5.6 0 0 1 7.2 0l1.6-1.9a8.1 8.1 0 0 0-10.4 0z" fill="#fff"/>',
  wifi_out: '<path d="M1.6 4.6l1.6 1.9a10.6 10.6 0 0 1 13.6 0l1.6-1.9a13.1 13.1 0 0 0-16.8 0z" fill="#fff"/>',
};
// d-pad: lit arm white, the rest at 38% (tinted white at draw time)
function dpad(lit) {
  const arm = (k, d) => `<path d="${d}" fill="#fff" fill-opacity="${lit === 'all' || lit === k ? 1 : 0.38}"/>`;
  return '<rect x="9" y="9" width="6" height="6" fill="#fff" fill-opacity="' + (lit === 'all' ? 1 : 0.38) + '"/>' +
    arm('up', 'M9 9V4.5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1V9l-3 2.6z') + arm('down', 'M9 15v4.5a1 1 0 0 0 1 1h4a1 1 0 0 0 1-1V15l-3-2.6z') +
    arm('left', 'M9 9H4.5a1 1 0 0 0-1 1v4a1 1 0 0 0 1 1H9l2.6-3z') + arm('right', 'M15 9h4.5a1 1 0 0 1 1 1v4a1 1 0 0 1-1 1H15l-2.6-3z');
}
const STICK = '<circle cx="12" cy="12" r="9.5" fill="none" stroke="#fff" stroke-width="1.8"/><circle cx="12" cy="12" r="4.3" fill="#fff"/>';
const FRONT = '<rect x="1.5" y="6" width="21" height="12" rx="2" fill="none" stroke="#fff" stroke-width="1.8"/><circle cx="14.5" cy="12" r="2.4" fill="#fff"/>';
const REAR = '<rect x="1.5" y="6" width="21" height="12" rx="2" fill="none" stroke="#fff" stroke-width="1.8"/><path d="M12 6v12M1.5 12h21" stroke="#fff" stroke-width="1.4"/>';

const items = [];
const add = (name, svg, w, h, vb = '0 0 24 24') => items.push({ name, svg, w, h, vb });
for (const s of [18, 22, 28]) {
  for (const k of ['disc', 'cross', 'circle', 'triangle', 'square']) add(`${k}_${s}`, P[k], s, s);
  for (const k of ['all', 'up', 'down', 'left', 'right']) add(`dpad_${k}_${s}`, dpad(k), s, s);
  add(`stick_${s}`, STICK, s, s);
  add(`front_touch_${s}`, FRONT, Math.round(s * 1.5), s, '0 0 24 24');
  add(`rear_touch_${s}`, REAR, Math.round(s * 1.5), s, '0 0 24 24');
}
add('pc_30', P.pc, 30, 30); add('pc_44', P.pc, 44, 44); add('pc_64', P.pc, 64, 64);
add('search_30', P.search, 30, 30); add('plus_30', P.plus, 30, 30);
add('gear_18', P.gear, 18, 18); add('power_18', P.power, 18, 18);
add('check_18', P.check, 18, 18); add('chevron_14', P.chevron, 14, 14);
add('sprint_22', P.sprint, 22, 22); add('moon_64', P.moon, 64, 64);
for (const k of Object.keys(WIFI)) { add(k + '_20', WIFI[k], 20, 16, '0 0 20 16'); add(k + '_22', WIFI[k], 22, 18, '0 0 20 16'); }

// shelf packer, 1 px gutter
const W = 256; let x = 1, y = 1, rowH = 0;
items.sort((a, b) => b.h - a.h || b.w - a.w);
for (const it of items) {
  if (x + it.w + 1 > W) { x = 1; y += rowH + 1; rowH = 0; }
  it.x = x; it.y = y; x += it.w + 1; rowH = Math.max(rowH, it.h);
}
const H = 256;
if (y + rowH > H) throw new Error('atlas overflow ' + (y + rowH));

const html = '<!doctype html><html><body style="margin:0;background:transparent">' +
  items.map(it => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${it.vb}" width="${it.w}" height="${it.h}" preserveAspectRatio="xMidYMid meet" style="position:absolute;left:${it.x}px;top:${it.y}px">${it.svg}</svg>`).join('') + '</body></html>';

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: true });
  const p = await b.newPage();
  await p.setViewport({ width: W, height: H, deviceScaleFactor: 1 });
  await p.setContent(html);
  await p.screenshot({ path: path.join(OUT, 'icons.png'), omitBackground: true, clip: { x: 0, y: 0, width: W, height: H } });
  // a dark-background preview for humans
  await p.evaluate(() => { document.body.style.background = '#1b202b'; });
  await p.screenshot({ path: path.join(OUT, 'icons-preview.png'), clip: { x: 0, y: 0, width: W, height: H } });
  await b.close();
  items.sort((a, b) => a.name.localeCompare(b.name));
  const enumName = (n) => 'MPH_ICON_' + n.toUpperCase();
  let h = '// Generated by atlas.js from the demo\'s SVGs. Don\'t edit; regenerate.\n// White glyphs on transparent in icons.png (256x256, RGBA). Tint when drawing.\n#pragma once\n\n';
  h += 'typedef enum {\n' + items.map(it => '  ' + enumName(it.name) + ',').join('\n') + '\n  MPH_ICON_COUNT\n} mph_icon_id;\n\n';
  h += 'typedef struct { unsigned short x, y, w, h; } mph_icon_rect;\n\n';
  h += 'static const mph_icon_rect MPH_ICON_RECTS[MPH_ICON_COUNT] = {\n' + items.map(it => `  [${enumName(it.name)}] = {${it.x}, ${it.y}, ${it.w}, ${it.h}},`).join('\n') + '\n};\n';
  fs.writeFileSync(path.join(OUT, 'mph_icons.h'), h);
  console.log(items.length, 'icons; atlas used to y =', y + rowH);
})();
