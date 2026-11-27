'use strict';
/*
 * 静态导出：把 EJS 渲染好的首页与 public/assets 复制到 dist-site/，
 * 方便丢给任意静态托管（Nginx、对象存储、GitHub Pages）。
 *
 *   npm run build
 */
const fs = require('fs');
const path = require('path');
const ejs = require('ejs');

const cfg = require('../src/config');

const ROOT = path.join(__dirname, '..');
const OUT = process.env.OUT_DIR || path.join(ROOT, 'dist-site');

function copyDir(from, to) {
  fs.mkdirSync(to, { recursive: true });
  for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
    const src = path.join(from, entry.name);
    const dst = path.join(to, entry.name);
    if (entry.isDirectory()) copyDir(src, dst);
    else fs.copyFileSync(src, dst);
  }
}

(async () => {
  const html = await ejs.renderFile(
    path.join(ROOT, 'views/index.ejs'),
    Object.assign({ year: new Date().getFullYear() }, cfg.collect())
  );

  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, 'index.html'), html);
  copyDir(path.join(ROOT, 'public/assets'), path.join(OUT, 'assets'));

  console.log('已导出静态站点：' + OUT);
  console.log('  index.html');
  console.log('  assets/（style.css、app.js、img/）');
  console.log('注意：静态托管没有 /download 路由，下载按钮需要改成直链或对象存储地址。');
})();
