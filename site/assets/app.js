/* ProperOS 站点交互 */
(function () {
  'use strict';

  var reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* 顶栏滚动状态 + 深色区反色 */
  var header = document.querySelector('.site-header');
  var darkZones = [document.getElementById('stack'), document.querySelector('.site-footer')].filter(Boolean);
  function onScroll() {
    if (!header) return;
    header.classList.toggle('is-stuck', window.scrollY > 8);
    var mid = 34;
    var onDark = darkZones.some(function (zone) {
      var r = zone.getBoundingClientRect();
      return r.top <= mid && r.bottom >= mid;
    });
    header.classList.toggle('on-dark', onDark);
  }
  onScroll();
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', onScroll, { passive: true });

  /* 移动端菜单 */
  var toggle = document.getElementById('menuToggle');
  var nav = document.getElementById('nav');
  if (toggle && nav) {
    toggle.addEventListener('click', function () {
      var open = nav.classList.toggle('is-open');
      toggle.setAttribute('aria-expanded', String(open));
      toggle.setAttribute('aria-label', open ? '关闭菜单' : '打开菜单');
    });
    nav.addEventListener('click', function (e) {
      if (e.target.tagName === 'A') {
        nav.classList.remove('is-open');
        toggle.setAttribute('aria-expanded', 'false');
      }
    });
  }

  /* 滚动渐显 */
  var revealables = document.querySelectorAll('.reveal');
  if (reduceMotion || !('IntersectionObserver' in window)) {
    revealables.forEach(function (el) { el.classList.add('is-in'); });
  } else {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        var el = entry.target;
        var siblings = Array.prototype.slice.call(el.parentNode.children);
        var i = siblings.indexOf(el);
        el.style.transitionDelay = Math.min(i, 6) * 55 + 'ms';
        el.classList.add('is-in');
        io.unobserve(el);
      });
    }, { rootMargin: '0px 0px -8% 0px', threshold: 0.12 });
    revealables.forEach(function (el) { io.observe(el); });
  }

  /* 安装器步骤切换 */
  var steps = document.querySelectorAll('.inst-step');
  var instImg = document.getElementById('instImage');
  var instCap = document.getElementById('instCaption');
  var captions = {
    '1': '步骤 1 / 4 · 欢迎',
    '2': '步骤 2 / 4 · 安装位置',
    '3': '步骤 3 / 4 · 账户与设备',
    '4': '步骤 4 / 4 · 确认安装',
    '5': '写入中 · 复制系统文件',
    '6': '完成 · 重启进入系统'
  };
  var alts = {
    '1': '安装器欢迎页：ProperOS 25.0.0，ARM64 / Linux 7.3.6，三项特性说明与开始安装按钮',
    '2': '安装器磁盘选择页：安装类型、目标磁盘单选与分区预览条',
    '3': '安装器账户页：设备名称、root 密码输入框与不设密码选项',
    '4': '安装器确认页：系统版本、目标磁盘、容量、设备名称、root 密码与覆盖警告',
    '5': '安装器写入进度页：正在复制系统文件，42% 进度条与安装日志',
    '6': '安装器完成页：系统已写入设备，提示移除介质并重启'
  };
  var shotFiles = {
    '1': 'welcome',
    '2': 'step-2',
    '3': 'step-3',
    '4': 'step-4',
    '5': 'step-5',
    '6': 'step-6'
  };
  var shots = {};
  steps.forEach(function (btn) {
    btn.addEventListener('click', function () {
      var n = btn.getAttribute('data-shot');
      steps.forEach(function (b) {
        var on = b === btn;
        b.classList.toggle('is-active', on);
        b.setAttribute('aria-selected', String(on));
      });
      if (!instImg) return;
      if (!shots[n]) shots[n] = 'assets/img/' + (shotFiles[n] || 'welcome') + '.webp';
      instImg.src = shots[n];
      instImg.alt = alts[n] || '';
      if (instCap) instCap.textContent = captions[n] || '';
    });
  });

  /* 上手方式 Tabs */
  var tabs = document.querySelectorAll('.tab');
  var panels = {
    flash: document.getElementById('panel-flash'),
    qemu: document.getElementById('panel-qemu'),
    build: document.getElementById('panel-build'),
    app: document.getElementById('panel-app')
  };
  tabs.forEach(function (tab) {
    tab.addEventListener('click', function () {
      var key = tab.getAttribute('data-tab');
      tabs.forEach(function (t) {
        var on = t === tab;
        t.classList.toggle('is-active', on);
        t.setAttribute('aria-selected', String(on));
      });
      Object.keys(panels).forEach(function (k) {
        var p = panels[k];
        if (!p) return;
        var on = k === key;
        p.classList.toggle('is-active', on);
        if (on) { p.removeAttribute('hidden'); } else { p.setAttribute('hidden', ''); }
      });
    });
  });

  /* 复制：优先 Clipboard API，file:// 下回退 execCommand */
  function legacyCopy(text) {
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.position = 'fixed';
    ta.style.top = '-1000px';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    var ok = false;
    try { ok = document.execCommand('copy'); } catch (err) { ok = false; }
    document.body.removeChild(ta);
    return ok;
  }

  function flash(btn, ok) {
    var prev = btn.textContent;
    btn.textContent = ok ? '已复制' : '复制失败';
    btn.classList.toggle('is-done', ok);
    clearTimeout(btn._t);
    btn._t = setTimeout(function () {
      btn.textContent = prev;
      btn.classList.remove('is-done');
    }, 1600);
  }

  document.addEventListener('click', function (e) {
    var btn = e.target.closest('.copy');
    if (!btn) return;
    var text = btn.getAttribute('data-copy');
    if (!text) {
      var targetId = btn.getAttribute('data-copy-target');
      var target = targetId && document.getElementById(targetId);
      if (target) text = target.innerText;
    }
    if (!text) return;

    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(function () {
        flash(btn, true);
      }, function () {
        flash(btn, legacyCopy(text));
      });
    } else {
      flash(btn, legacyCopy(text));
    }
  });

  /* 年份 */
  var year = document.getElementById('year');
  if (year) year.textContent = String(new Date().getFullYear());
})();
