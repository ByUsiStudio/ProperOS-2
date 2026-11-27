'use strict';
/*
 * ProperOS 25.0.0 官网服务
 *
 *   npm install && npm start          → http://127.0.0.1:3000
 *   PORT=8080 npm start               → 换端口
 *   DL_DIR=/srv/properos/dist npm start → 指定产物目录（默认仓库里的 dist/）
 *
 * 页面由 EJS 服务端渲染，静态资源走 public/assets。
 * 下载路由把 dist/ 里的产物以附件形式发出，避免浏览器直接播放 ISO。
 */
const path = require('path');
const express = require('express');
const compression = require('compression');
const fs = require('fs');

const cfg = require('./src/config');

const app = express();
const PORT = Number(process.env.PORT || 3000);
const HOST = process.env.HOST || '0.0.0.0';
const PUBLIC_DIR = path.join(__dirname, 'public');

app.disable('x-powered-by');
app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, 'views'));

app.use(compression());
app.use((req, res, next) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'strict-origin-when-cross-origin');
  next();
});

/* 静态资源：图片、样式、脚本。文件名不带哈希，缓存一天，协商 ETag。 */
app.use(
  '/assets',
  express.static(path.join(PUBLIC_DIR, 'assets'), {
    maxAge: '1d',
    etag: true,
    index: false,
  })
);

app.get('/', (req, res) => {
  res.render('index', Object.assign({ year: new Date().getFullYear() }, cfg.collect()));
});

app.get('/healthz', (req, res) => {
  res.json({ ok: true, version: cfg.VERSION });
});

/* 下载：只允许配置里登记过的文件名，防止路径穿越 */
app.get('/download/:file', (req, res) => {
  const item = cfg.PRODUCTS.find((p) => p.file === req.params.file);
  if (!item) return res.status(404).send('未找到该产物');

  const abs = path.join(cfg.DL_DIR, item.file);
  if (!fs.existsSync(abs)) {
    return res
      .status(404)
      .type('text/plain; charset=utf-8')
      .send('产物尚未发布到服务器：' + item.file);
  }
  res.download(abs, item.file);
});

app.use((req, res) => {
  res.status(404).type('text/plain; charset=utf-8').send('404 · 页面不存在\n返回首页：/');
});

app.listen(PORT, HOST, () => {
  const info = cfg.collect();
  const listed = cfg.PRODUCTS.map(
    (p) => `  ${p.file}  ${info.size[p.key]}${info.dl[p.key].exists ? '' : '（未就位）'}`
  ).join('\n');
  console.log(`ProperOS ${cfg.VERSION} 官网已启动：http://127.0.0.1:${PORT}`);
  console.log('产物目录：' + cfg.DL_DIR + '\n' + listed);
});
