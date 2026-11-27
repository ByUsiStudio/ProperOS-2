'use strict';
/*
 * 站点配置：版本、构建日期、可下载产物。
 *
 * 产物信息优先从 dist/ 里的真实文件读取（存在时取文件大小与 .sha256），
 * 读不到则退回下面写死的值，保证站点在只有源码的机器上也能跑起来。
 */
const fs = require('fs');
const path = require('path');

const VERSION = '25.0.0';
const BUILD_DATE = '2026-11-27';

const DL_DIR = process.env.DL_DIR || path.resolve(__dirname, '../../dist');

const PRODUCTS = [
  {
    key: 'iso',
    file: 'ProperOS-25.0.0-arm64.iso',
    size: '769 MB',
    sha: '6433cfc11726c5d9d089df45f7c90176756def00f5e831b032538ca541b85d66',
  },
  {
    key: 'img',
    file: 'ProperOS-25.0.0-arm64.img',
    size: '7.3 GB',
    sha: '5858179f0692dcb5c80a471bb82bc7ebc00f7f3f9d2729c4ce2de1dfd217fcf7',
  },
  {
    key: 'source',
    file: 'ProperOS-25.0.0-source.zip',
    size: '3.0 MB',
    sha: '0fb39e2f8968dcc5cfac83d0a2720fc669caecdb473d065741cd4e215cf317f1',
  },
];

function humanSize(bytes) {
  const mb = bytes / (1024 * 1024);
  if (mb >= 1024) return (mb / 1024).toFixed(1) + ' GB';
  return Math.round(mb) + ' MB';
}

function readSha(file) {
  try {
    return fs.readFileSync(file + '.sha256', 'utf8').trim().split(/\s+/)[0];
  } catch {
    return null;
  }
}

function collect() {
  const dl = {};
  const sha = {};
  const size = {};

  for (const p of PRODUCTS) {
    const abs = path.join(DL_DIR, p.file);
    let realSize = null;
    try {
      realSize = fs.statSync(abs).size;
    } catch {
      /* 产物不在本机：保留配置里的值 */
    }
    dl[p.key] = {
      file: p.file,
      href: '/download/' + p.file,
      exists: realSize !== null,
    };
    sha[p.key] = readSha(abs) || p.sha;
    size[p.key] = realSize ? humanSize(realSize) : p.size;
  }

  return { dl, sha, size };
}

module.exports = {
  VERSION,
  BUILD_DATE,
  DL_DIR,
  PRODUCTS,
  collect,
};
