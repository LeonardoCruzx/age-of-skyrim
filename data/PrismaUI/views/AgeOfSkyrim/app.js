// M11: mapa tático do Age of Skyrim (view do PrismaUI). ADR 0051-0054; schema em docs/warmap-schema.md.
//  - C++ -> JS: window.aosWarMapData(json) (snapshot completo, só ao abrir e quando muda) e
//               window.aosWarMapResult(json) ({ok, text} de uma ação).
//  - JS -> C++: window.aosWarMapAction(JSON.stringify({action, ...})) — registrado pelo DLL.
//  - Fora do jogo: index.html?sample (carrega sample/sample-data.js e simula as ações). ?lang=en|pt-br.
(function () {
  'use strict';

  const SVG = 'http://www.w3.org/2000/svg';
  const W = 1000;
  const PAD = 40;

  const state = {
    data: null,
    byId: new Map(),
    factions: new Map(),
    provinces: new Map(),
    world: null,
    selected: null,
    routeFrom: null,
    search: '',
    view: { x: 0, y: 0, w: W, h: W },
    sample: false,
  };

  const $ = (id) => document.getElementById(id);

  // ---- textos -----------------------------------------------------------------------------------------
  function t(key, ...args) {
    const raw = (state.data && state.data.i18n && state.data.i18n[key]) || key;
    let i = 0;
    return raw.replace(/\{\}/g, () => (i < args.length ? String(args[i++]) : ''));
  }

  function applyStaticTexts() {
    document.querySelectorAll('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });
    document.querySelectorAll('[data-i18n-placeholder]').forEach((el) => { el.placeholder = t(el.dataset.i18nPlaceholder); });
    document.documentElement.lang = (state.data && state.data.lang) || 'en';
  }

  // ---- utilidades -------------------------------------------------------------------------------------
  function el(tag, attrs, text) {
    const e = document.createElement(tag);
    if (attrs) for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function svg(tag, attrs) {
    const e = document.createElementNS(SVG, tag);
    if (attrs) for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    return e;
  }

  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

  function ownerColor(owner) {
    const f = owner && state.factions.get(owner);
    return f ? f.color : '#8a8170';
  }

  function ownerName(owner) {
    const f = owner && state.factions.get(owner);
    return f ? f.name : t('warmap.ui.neutral');
  }

  function isMine(terr) {
    return !!(terr && terr.owner && state.data.playerFaction && terr.owner === state.data.playerFaction);
  }

  function hashHue(s) {
    let h = 0;
    for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return h % 360;
  }

  function toast(text, ok) {
    const box = $('toast');
    box.textContent = text;
    box.classList.toggle('fail', !ok);
    box.hidden = false;
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => { box.hidden = true; }, 3500);
  }

  // ---- projeção: coordenadas do jogo (Y para o norte) -> SVG (Y para baixo) ------------------------
  let proj = null;
  function makeProjection(ws) {
    const b = ws || { minX: 0, minY: 0, maxX: 1, maxY: 1 };
    const spanX = Math.max(1, b.maxX - b.minX);
    const spanY = Math.max(1, b.maxY - b.minY);
    const scale = (W - 2 * PAD) / Math.max(spanX, spanY);
    const offX = PAD + ((W - 2 * PAD) - spanX * scale) / 2;
    const offY = PAD + ((W - 2 * PAD) - spanY * scale) / 2;
    return (x, y) => [offX + (x - b.minX) * scale, offY + (b.maxY - y) * scale];
  }

  // Fecho convexo (monotone chain) para desenhar a área de cada província.
  function hull(points) {
    const p = points.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    if (p.length < 3) return p;
    const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
    const lower = [];
    for (const q of p) { while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], q) <= 0) lower.pop(); lower.push(q); }
    const upper = [];
    for (let i = p.length - 1; i >= 0; i--) { const q = p[i]; while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], q) <= 0) upper.pop(); upper.push(q); }
    upper.pop(); lower.pop();
    return lower.concat(upper);
  }

  // Ícone por tipo (formas simples, centradas em 0,0).
  function icon(type, color) {
    let e;
    switch (type) {
      case 'city': e = svg('rect', { x: -8, y: -8, width: 16, height: 16, rx: 2 }); break;
      case 'fort': e = svg('path', { d: 'M-7,-7 H7 V2 L0,8 L-7,2 Z' }); break;
      case 'tower': e = svg('path', { d: 'M0,-9 L7,7 H-7 Z' }); break;
      case 'camp': e = svg('path', { d: 'M0,-6 L7,5 H-7 Z M-2,5 L0,1 L2,5' }); break;
      case 'village': e = svg('circle', { r: 6 }); break;
      case 'claim': e = svg('path', { d: 'M-4,8 V-8 H6 L3,-4 L6,0 H-4' }); break;
      default: e = svg('circle', { r: 4 }); break;
    }
    e.setAttribute('class', 'icon');
    e.setAttribute('fill', color);
    return e;
  }

  // ---- desenho --------------------------------------------------------------------------------------
  function visibleTerritories() {
    return state.data.territories.filter((x) => x.ws === state.world && x.x !== null && x.y !== null);
  }

  function render() {
    const d = state.data;
    const ws = d.worldspaces.find((w) => w.id === state.world);
    proj = makeProjection(ws);
    const terrs = visibleTerritories();
    const pos = new Map(terrs.map((x) => [x.id, proj(x.x, x.y)]));

    // Províncias
    const gp = $('l-provinces'); clear(gp);
    const groups = new Map();
    for (const x of terrs) {
      if (!x.province) continue;
      if (!groups.has(x.province)) groups.set(x.province, []);
      groups.get(x.province).push(pos.get(x.id));
    }
    for (const [pid, pts] of groups) {
      const hue = hashHue(pid);
      const h = hull(pts);
      if (h.length >= 3) {
        gp.appendChild(svg('polygon', { class: 'province', points: h.map((q) => q.join(',')).join(' '),
          fill: `hsl(${hue},45%,45%)`, stroke: `hsl(${hue},45%,30%)` }));
      }
      const cx = pts.reduce((s, q) => s + q[0], 0) / pts.length;
      const cy = pts.reduce((s, q) => s + q[1], 0) / pts.length;
      const label = svg('text', { class: 'province-label', x: cx, y: cy });
      label.textContent = (state.provinces.get(pid) || {}).name || '';
      gp.appendChild(label);
    }

    // Vizinhança (cada par uma vez)
    const gn = $('l-neighbors'); clear(gn);
    const seen = new Set();
    for (const x of terrs) {
      for (const n of x.neighbors) {
        const key = x.id < n ? x.id + n : n + x.id;
        if (seen.has(key) || !pos.has(n)) continue;
        seen.add(key);
        const [x1, y1] = pos.get(x.id); const [x2, y2] = pos.get(n);
        gn.appendChild(svg('line', { class: 'neighbor', x1, y1, x2, y2 }));
      }
    }

    // Rotas
    const gr = $('l-routes'); clear(gr);
    for (const r of d.routes) {
      if (!pos.has(r.a) || !pos.has(r.b)) continue;
      const [x1, y1] = pos.get(r.a); const [x2, y2] = pos.get(r.b);
      const cls = !r.active ? 'inactive' : r.blocked ? 'blocked' : 'active';
      const line = svg('line', { class: `route ${cls}`, x1, y1, x2, y2, 'data-id': r.id, 'data-a': r.a, 'data-b': r.b });
      const title = svg('title'); title.textContent = routeText(r); line.appendChild(title);
      gr.appendChild(line);
    }

    // Territórios
    const gt = $('l-territories'); clear(gt);
    for (const x of terrs) {
      const [px, py] = pos.get(x.id);
      const major = x.type === 'city' || x.seat || x.target || isMine(x);
      const g = svg('g', { class: `terr${major ? '' : ' minor'}`, transform: `translate(${px.toFixed(1)},${py.toFixed(1)})`, 'data-id': x.id });
      if (x.seat) g.appendChild(svg('circle', { class: 'ring-seat', r: 13 }));
      if (x.target) g.appendChild(svg('circle', { class: 'ring-target', r: 11 }));
      g.appendChild(icon(x.type, ownerColor(x.owner)));
      const label = svg('text', { class: 'label', y: 20 }); label.textContent = x.name; g.appendChild(label);
      const title = svg('title'); title.textContent = `${x.name} (${t('warmap.type.' + x.type)}) - ${ownerName(x.owner)}`; g.appendChild(title);
      g.addEventListener('click', (ev) => { ev.stopPropagation(); onTerritoryClick(x.id); });
      gt.appendChild(g);
    }

    // Jogador
    const gpl = $('l-player'); clear(gpl);
    if (d.player && d.player.ws === state.world) {
      const [px, py] = proj(d.player.x, d.player.y);
      const c = svg('circle', { class: 'player', cx: px, cy: py, r: 6 });
      const title = svg('title'); title.textContent = t('warmap.ui.you'); c.appendChild(title);
      gpl.appendChild(c);
    }

    toggleLayers();
    applyHighlights();
    renderPanel();
    renderSeat();
    renderLegend();
  }

  function toggleLayers() {
    $('l-provinces').style.display = $('t-provinces').checked ? '' : 'none';
    $('l-neighbors').style.display = $('t-neighbors').checked ? '' : 'none';
    $('l-routes').style.display = $('t-routes').checked ? '' : 'none';
  }

  // Seleção / busca só trocam classes (sem redesenhar).
  function applyHighlights() {
    const q = state.search.trim().toLowerCase();
    document.querySelectorAll('#l-territories .terr').forEach((g) => {
      const x = state.byId.get(g.dataset.id);
      g.classList.toggle('sel', g.dataset.id === state.selected);
      const match = q && x && x.name.toLowerCase().includes(q);
      g.classList.toggle('match', !!match);
      g.classList.toggle('dim', !!q && !match);
    });
    document.querySelectorAll('#l-routes .route').forEach((l) => {
      l.classList.toggle('hl', !!state.selected && (l.dataset.a === state.selected || l.dataset.b === state.selected));
    });
  }

  function routeText(r) {
    const a = state.byId.get(r.a); const b = state.byId.get(r.b);
    const status = !r.active ? t('warmap.ui.routeInactive')
      : r.blocked ? t('warmap.ui.routeBlocked', (state.byId.get(r.blocker) || {}).name || '?')
        : t('warmap.ui.routeActive');
    return `${t('warmap.ui.routeLine', a ? a.name : '?', b ? b.name : '?')} - ${status}`;
  }

  function layerValue(v) {
    if (v === null || v === undefined) return '';
    if (typeof v !== 'object') return String(v);
    if (Array.isArray(v)) return v.map(layerValue).join(', ');
    return Object.entries(v).map(([k, x]) => `${k}: ${layerValue(x)}`).join(' · ');
  }

  // ---- painel do território -------------------------------------------------------------------------
  function renderPanel() {
    const p = $('panel'); clear(p);
    const x = state.selected && state.byId.get(state.selected);
    if (!x) { p.appendChild(el('p', { class: 'muted' }, t('warmap.ui.noSelection'))); return; }

    const h = el('h2', null, x.name);
    if (x.seat) { h.appendChild(document.createTextNode(' ')); h.appendChild(el('span', { class: 'tag seat' }, t('warmap.ui.seat'))); }
    if (x.target) { h.appendChild(document.createTextNode(' ')); h.appendChild(el('span', { class: 'tag target' }, t('warmap.ui.target'))); }
    p.appendChild(h);

    const dl = el('dl');
    const row = (k, v) => { dl.appendChild(el('dt', null, k)); const dd = el('dd'); if (v instanceof Node) dd.appendChild(v); else dd.textContent = v; dl.appendChild(dd); };
    row(t('warmap.ui.type'), t('warmap.type.' + x.type));
    row(t('warmap.ui.province'), (x.province && (state.provinces.get(x.province) || {}).name) || '-');
    const owner = el('span'); owner.appendChild(el('span', { class: 'chip', style: `background:${ownerColor(x.owner)}` }));
    owner.appendChild(document.createTextNode(' ' + ownerName(x.owner)));
    row(t('warmap.ui.owner'), owner);
    p.appendChild(dl);

    // Ações
    const actions = el('div', { class: 'actions' });
    if (isMine(x)) {
      const b = el('button', null, t('warmap.ui.createRoute'));
      b.addEventListener('click', () => startRoute(x.id));
      actions.appendChild(b);
    } else {
      const b = el('button', { class: x.target ? '' : 'danger' }, t(x.target ? 'warmap.ui.unmarkTarget' : 'warmap.ui.markTarget'));
      b.addEventListener('click', () => send({ action: 'target', territory: x.id, on: !x.target }));
      actions.appendChild(b);
    }
    p.appendChild(actions);

    // Rotas que passam por ele
    const routes = state.data.routes.filter((r) => r.a === x.id || r.b === x.id);
    p.appendChild(el('h3', null, t('warmap.ui.routes', state.data.routes.length, state.data.limits.maxRoutes)));
    if (!routes.length) p.appendChild(el('p', { class: 'muted' }, t('warmap.ui.noRoutes')));
    const ul = el('ul');
    for (const r of routes) {
      const li = el('li');
      const cls = !r.active ? 'route-inactive' : r.blocked ? 'route-blocked' : 'route-active';
      const span = el('span', { class: `grow ${cls}` }, routeText(r));
      span.appendChild(el('br'));
      span.appendChild(el('small', null, t('warmap.ui.routeStats', Math.round(r.distance / 70), r.goldPerHour.toFixed(1))));
      li.appendChild(span);
      if (r.owner && r.owner === state.data.playerFaction) {
        const b = el('button', { class: 'danger' }, t('warmap.ui.removeRoute'));
        b.addEventListener('click', () => send({ action: 'removeRoute', route: r.id }));
        li.appendChild(b);
      }
      ul.appendChild(li);
    }
    p.appendChild(ul);

    // Vizinhos
    if (x.neighbors.length) {
      p.appendChild(el('h3', null, t('warmap.ui.neighbors')));
      const nl = el('ul');
      for (const n of x.neighbors) {
        const nt = state.byId.get(n);
        if (!nt) continue;
        const li = el('li');
        li.appendChild(el('span', { class: 'chip', style: `background:${ownerColor(nt.owner)}` }));
        const a = el('span', { class: 'link' }, nt.name);
        a.addEventListener('click', () => select(n));
        li.appendChild(a);
        nl.appendChild(li);
      }
      p.appendChild(nl);
    }

    // Camadas extras (guarnição, ameaça... registradas por outros módulos)
    const keys = Object.keys(x.layers || {});
    if (keys.length) {
      p.appendChild(el('h3', null, t('warmap.ui.layers')));
      const ldl = el('dl');
      for (const k of keys) {
        const meta = state.data.layers.find((l) => l.key === k);
        ldl.appendChild(el('dt', null, meta ? meta.label : k));
        ldl.appendChild(el('dd', null, layerValue(x.layers[k])));
      }
      p.appendChild(ldl);
    }
  }

  function renderSeat() {
    const s = $('seat'); clear(s);
    const d = state.data;
    s.appendChild(el('p', null, t('warmap.ui.targets', d.territories.filter((x) => x.target).length, d.limits.maxTargets)));
    if (!d.seat) return;
    const seatT = state.byId.get(d.seat.territory);
    const h = el('h3', null, t('warmap.ui.stock'));
    s.appendChild(h);
    if (seatT) {
      const a = el('p', { class: 'link' }, seatT.name);
      a.addEventListener('click', () => select(seatT.id));
      s.appendChild(a);
    }
    const dl = el('dl');
    for (const c of d.seat.stock) { dl.appendChild(el('dt', null, c.name)); dl.appendChild(el('dd', null, String(c.count))); }
    if (d.seat.unclassified) { dl.appendChild(el('dt', null, t('warmap.ui.unclassified'))); dl.appendChild(el('dd', null, String(d.seat.unclassified))); }
    s.appendChild(dl);
  }

  function renderLegend() {
    const l = $('legend'); clear(l);
    l.appendChild(el('h3', null, t('warmap.ui.legend')));
    const ul = el('ul');
    const owners = new Set(state.data.territories.map((x) => x.owner).filter(Boolean));
    for (const f of state.data.factions) {
      if (!owners.has(f.id) && !f.player) continue;
      const li = el('li');
      li.appendChild(el('span', { class: 'chip', style: `background:${f.color}` }));
      li.appendChild(document.createTextNode(f.name));
      ul.appendChild(li);
    }
    const li = el('li');
    li.appendChild(el('span', { class: 'chip', style: `background:${ownerColor(null)}` }));
    li.appendChild(document.createTextNode(t('warmap.ui.neutral')));
    ul.appendChild(li);
    l.appendChild(ul);
    const types = el('ul');
    for (const type of ['city', 'fort', 'tower', 'camp', 'village', 'claim']) {
      const li2 = el('li');
      const s = svg('svg', { width: 20, height: 20, viewBox: '-10 -10 20 20' });
      s.appendChild(icon(type, '#bfae8a'));
      li2.appendChild(s);
      li2.appendChild(document.createTextNode(t('warmap.type.' + type)));
      types.appendChild(li2);
    }
    l.appendChild(types);
  }

  // ---- interação ------------------------------------------------------------------------------------
  function select(id) {
    state.selected = id;
    const x = state.byId.get(id);
    if (x && x.ws && x.ws !== state.world) { state.world = x.ws; $('world').value = x.ws; render(); return; }
    applyHighlights();
    renderPanel();
  }

  function onTerritoryClick(id) {
    if (state.routeFrom) {
      const from = state.routeFrom;
      endRoute();
      if (from !== id) send({ action: 'createRoute', a: from, b: id });
      return;
    }
    select(id);
  }

  function startRoute(id) {
    state.routeFrom = id;
    const hint = $('pick-hint');
    hint.textContent = t('warmap.ui.pickSecond');
    hint.hidden = false;
    $('map').classList.add('picking');
  }

  function endRoute() {
    state.routeFrom = null;
    $('pick-hint').hidden = true;
    $('map').classList.remove('picking');
  }

  function send(msg) {
    if (state.sample) { SampleBackend.handle(msg); return; }
    if (typeof window.aosWarMapAction === 'function') window.aosWarMapAction(JSON.stringify(msg));
  }

  // Zoom (roda) e arrasto (botão esquerdo) mudando só o viewBox.
  function setViewBox() {
    const v = state.view;
    $('map').setAttribute('viewBox', `${v.x} ${v.y} ${v.w} ${v.h}`);
    $('map').classList.toggle('zoomed', v.w < W / 1.8);
  }

  function setupPanZoom() {
    const map = $('map');
    map.addEventListener('wheel', (ev) => {
      ev.preventDefault();
      const r = map.getBoundingClientRect();
      const v = state.view;
      const k = ev.deltaY > 0 ? 1.15 : 1 / 1.15;
      const nw = Math.min(W * 2, Math.max(W / 12, v.w * k));
      const fx = (ev.clientX - r.left) / r.width; const fy = (ev.clientY - r.top) / r.height;
      v.x += (v.w - nw) * fx; v.y += (v.h - nw) * fy; v.w = nw; v.h = nw;
      setViewBox();
    }, { passive: false });
    let drag = null;
    map.addEventListener('mousedown', (ev) => { if (ev.button === 0) drag = { x: ev.clientX, y: ev.clientY, moved: false }; });
    window.addEventListener('mousemove', (ev) => {
      if (!drag) return;
      const r = map.getBoundingClientRect();
      const s = state.view.w / Math.min(r.width, r.height);
      const dx = ev.clientX - drag.x; const dy = ev.clientY - drag.y;
      if (Math.abs(dx) + Math.abs(dy) > 3) { drag.moved = true; map.classList.add('dragging'); }
      state.view.x -= dx * s; state.view.y -= dy * s;
      drag.x = ev.clientX; drag.y = ev.clientY;
      setViewBox();
    });
    window.addEventListener('mouseup', () => { drag = null; map.classList.remove('dragging'); });
  }

  function setupControls() {
    $('close').addEventListener('click', () => send({ action: 'close' }));
    ['t-provinces', 't-neighbors', 't-routes'].forEach((id) => $(id).addEventListener('change', toggleLayers));
    $('world').addEventListener('change', (ev) => { state.world = ev.target.value; render(); });
    $('search').addEventListener('input', (ev) => { state.search = ev.target.value; applyHighlights(); });
    $('search').addEventListener('keydown', (ev) => {
      if (ev.key !== 'Enter') return;
      const q = state.search.trim().toLowerCase();
      const hit = q && state.data && state.data.territories.find((x) => x.name.toLowerCase().includes(q));
      if (hit) select(hit.id);
    });
    window.addEventListener('keydown', (ev) => {
      if (ev.key !== 'Escape') return;
      ev.preventDefault();
      if (state.routeFrom) endRoute();
      else send({ action: 'close' });
    });
  }

  // ---- entrada de dados (chamado pelo DLL) ----------------------------------------------------------
  window.aosWarMapData = function (json) {
    let d;
    try { d = typeof json === 'string' ? JSON.parse(json) : json; } catch (e) { console.error('aosWarMapData: JSON inválido', e); return; }
    if (!d || d.schema !== 1) { console.error('aosWarMapData: schema desconhecido', d && d.schema); return; }
    state.data = d;
    state.byId = new Map(d.territories.map((x) => [x.id, x]));
    state.factions = new Map(d.factions.map((f) => [f.id, f]));
    state.provinces = new Map(d.provinces.map((p) => [p.id, p]));
    if (!d.worldspaces.some((w) => w.id === state.world)) state.world = d.worldspace || (d.worldspaces[0] || {}).id || null;
    if (state.selected && !state.byId.has(state.selected)) state.selected = null;
    if (!state.selected && d.seat) state.selected = d.seat.territory;
    const sel = $('world'); clear(sel);
    for (const w of d.worldspaces) { const o = el('option', { value: w.id }, `${w.name} (${w.count})`); sel.appendChild(o); }
    if (state.world) sel.value = state.world;
    applyStaticTexts();
    render();
  };

  window.aosWarMapResult = function (json) {
    let r;
    try { r = typeof json === 'string' ? JSON.parse(json) : json; } catch (e) { return; }
    if (r && r.text) toast(r.text, !!r.ok);
  };

  // ---- modo de exemplo (fora do jogo) ---------------------------------------------------------------
  // Simula as ações do DLL sobre o snapshot de exemplo, para testar a view num navegador.
  const SampleBackend = {
    handle(msg) {
      const d = state.data;
      const reply = (ok, text) => { window.aosWarMapResult(JSON.stringify({ ok, text })); window.aosWarMapData(JSON.stringify(d)); };
      if (msg.action === 'close') { toast('close', true); return; }
      if (msg.action === 'target') {
        const x = d.territories.find((y) => y.id === msg.territory);
        if (x) x.target = !!msg.on;
        reply(true, t(msg.on ? 'warmap.targetAdded' : 'warmap.targetRemoved', x ? x.name : '?'));
      } else if (msg.action === 'createRoute') {
        const a = d.territories.find((y) => y.id === msg.a); const b = d.territories.find((y) => y.id === msg.b);
        if (!isMine(a) || !isMine(b)) { reply(false, t('warmap.routeNotYours')); return; }
        const id = (parseInt(msg.a, 16) ^ parseInt(msg.b, 16)).toString(16).toUpperCase().padStart(8, '0');
        if (d.routes.some((r) => r.id === id)) { reply(false, t('route.duplicate')); return; }
        const dist = Math.hypot(a.x - b.x, a.y - b.y);
        d.routes.push({ id, a: a.id, b: b.id, owner: a.owner, active: true, blocked: false, blocker: null, distance: dist, goldPerHour: 0.5 + 0.5 * dist / 10000 });
        reply(true, t('route.ok'));
      } else if (msg.action === 'removeRoute') {
        d.routes = d.routes.filter((r) => r.id !== msg.route);
        reply(true, t('warmap.routeRemoved'));
      }
    },
  };

  function boot() {
    setupControls();
    setupPanZoom();
    const params = new URLSearchParams(location.search);
    if (params.has('sample') || window.AOS_SAMPLE_MODE) {
      state.sample = true;
      $('sample-badge').hidden = false;
      const lang = params.get('lang') || 'pt-br';
      const s = document.createElement('script');
      s.src = 'sample/sample-data.js';
      s.onload = () => {
        const d = JSON.parse(JSON.stringify(window.AOS_SAMPLE_DATA));
        if (window.AOS_SAMPLE_I18N && window.AOS_SAMPLE_I18N[lang]) { d.i18n = window.AOS_SAMPLE_I18N[lang]; d.lang = lang; }
        window.aosWarMapData(JSON.stringify(d));
      };
      s.onerror = () => console.error('sample/sample-data.js não encontrado: rode tools/warmap_sample.py');
      document.head.appendChild(s);
      return;
    }
    send({ action: 'ready' });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
