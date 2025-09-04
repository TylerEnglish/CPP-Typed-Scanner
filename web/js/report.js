/* web/js/report.js */

function showError(msg){
  const div = document.createElement('div');
  div.style.cssText = "position:fixed;bottom:10px;left:10px;background:#c62828;color:#fff;padding:8px 12px;border-radius:8px;z-index:9999;font:12px/1.4 system-ui";
  div.textContent = "Report error: " + msg;
  document.body.appendChild(div);
  console.error("[report] " + msg);
}

function sanitizeJsonText(txt){
  return (txt || "")
    .replace(/\bNaN\b/gi,"0")
    .replace(/\b-Infinity\b/g,"0")
    .replace(/\bInfinity\b/g,"0");
}

async function getCtx(){
  try {
    const el = document.getElementById('run-data');
    const raw = sanitizeJsonText(el ? el.textContent : "{}");
    return JSON.parse(raw || "{}");
  } catch (e) {
    console.warn("inline JSON parse failed, trying run.json:", e);
  }
  try {
    const r = await fetch("./run.json", { cache: "no-store" });
    const t = sanitizeJsonText(await r.text());
    return JSON.parse(t || "{}");
  } catch (e2) {
    showError("Could not parse inline JSON or fetch run.json");
    return {};
  }
}

const $ = (sel) => document.querySelector(sel);
const fmt = {
  int: (n) => (n ?? 0).toLocaleString(),
  num: (n, d=2) => (n ?? 0).toLocaleString(undefined, { maximumFractionDigits: d }),
  ms:  (n) => `${fmt.num(n, 0)} ms`,
  pct: (n) => `${fmt.num(n, 1)}%`,
  mb:  (n) => `${fmt.num(n, 2)} MB`
};

const delta = (after, before, invert=false) => {
  if (before == null || after == null) return { val: 0, dir: 'flat', pct: 0 };
  const raw = after - before;
  const better = invert ? raw < 0 : raw > 0;
  const dir = raw === 0 ? 'flat' : (better ? 'up' : 'down');
  const pct = before === 0 ? 0 : (raw / before) * 100;
  return { val: raw, pct, dir };
};

const chip = (d, unit='') => {
  const s = d.dir === 'up' ? '▲' : d.dir === 'down' ? '▼' : '•';
  const cls = `delta ${d.dir}`;
  const pct = isFinite(d.pct) ? fmt.num(d.pct, 1) : '0.0';
  return `<span class="${cls}" title="${fmt.num(d.val)}${unit}">${s} ${pct}%</span>`;
};

function mountKPIs(current, baseline){
  const map = [
    ['#kpi-rows',   fmt.int(current.rows)],
    ['#kpi-mbs',    fmt.num(current.throughput_mb_s)],
    ['#kpi-tokens', fmt.num(current.tokens_per_sec)],
    ['#kpi-allocs', fmt.num(current.allocs_per_sec)],
    ['#kpi-p95',    fmt.ms(current.p95_ms)],
    ['#kpi-rss',    fmt.mb(current.peak_rss_mb)],
    ['#kpi-cpu',    fmt.pct(current.cpu_pct)],
    ['#kpi-file',   current.filename || '—'],
    ['#kpi-type',   current.content_type || '—'],
    ['#kpi-size',   fmt.int(current.file_size)],
    ['#kpi-etag',   current.etag || '—'],
  ];
  for (const [sel, val] of map) { const el = $(sel); if (el) el.textContent = val; }

  const host = $('#compare-kpis');
  if (!host || !baseline) return;
  const items = [
    { label: 'Tokens/sec',    a: current.tokens_per_sec,   b: baseline.tokens_per_sec,   unit: '',   invert: false },
    { label: 'MB/s',          a: current.throughput_mb_s,  b: baseline.throughput_mb_s,  unit: '',   invert: false },
    { label: 'p95 (ms)',      a: current.p95_ms,           b: baseline.p95_ms,           unit: ' ms',invert: true },
    { label: 'Peak RSS (MB)', a: current.peak_rss_mb,      b: baseline.peak_rss_mb,      unit: ' MB',invert: true },
    { label: 'CPU %',         a: current.cpu_pct,          b: baseline.cpu_pct,          unit: '%',  invert: true },
    { label: 'Allocs/sec',    a: current.allocs_per_sec,   b: baseline.allocs_per_sec,   unit: '',   invert: true },
  ];
  host.innerHTML = items.map(it => {
    const d = delta(it.a, it.b, it.invert);
    return `<div class="kpi-compare">
      <div class="kpi-compare__label">${it.label}</div>
      <div class="kpi-compare__vals">
        <span class="kpi-compare__after">${fmt.num(it.a)}</span>
        <span class="kpi-compare__sep"> vs </span>
        <span class="kpi-compare__before">${fmt.num(it.b)}</span>
        ${chip(d, it.unit)}
      </div>
    </div>`;
  }).join('');
}

function asStageRows(r){
  return (r.stage_times || []).map(s => ({ stage: s.stage, duration_ms: s.duration_ms }));
}



function hexToRgba(hex, a = 1){
  const m = (hex || "").trim().replace('#','');
  if (m.length !== 6 && m.length !== 8) return `rgba(0,0,0,${a})`;
  const r = parseInt(m.slice(0,2),16), g = parseInt(m.slice(2,4),16), b = parseInt(m.slice(4,6),16);
  return `rgba(${r},${g},${b},${a})`;
}

function themeConfigFromCSS(){
  const cs      = getComputedStyle(document.documentElement);
  const text    = (cs.getPropertyValue('--text')   || '#e6ebf3').trim();
  const muted   = (cs.getPropertyValue('--muted')  || '#9aa3b2').trim();
  const border  = (cs.getPropertyValue('--border') || '#223047').trim();
  const accent  = (cs.getPropertyValue('--accent') || '#7aa2f7').trim();
  const accent2 = (cs.getPropertyValue('--accent-2') || '#9b8cff').trim();

  const gridColor   = hexToRgba(border, 0.55);
  const domainColor = hexToRgba(border, 0.85);

  return {
    background: null,
    view:   { stroke: gridColor },
    axis:   {
      labelColor: text,
      titleColor: text,
      gridColor: gridColor,
      tickColor: domainColor,
      domainColor: domainColor,
      labelFontSize: 12,
      titleFontSize: 12,
      grid: true
    },
    legend: {
      labelColor: text,
      titleColor: text,
      labelFontSize: 12,
      titleFontSize: 12,
      symbolType: "stroke"
    },
    range: { category: [accent, accent2, "#22c55e", "#f59e0b", "#ef4444", "#06b6d4", "#e879f9"] },
    line:  { strokeWidth: 2.2 },
    point: { filled: true, size: 48 },
    bar:   { cornerRadiusEnd: 3 }
  };
}

// Remove the entire chart card when there’s no data
function removeCardFor(el) {
  const host = typeof el === "string" ? document.querySelector(el) : el;
  if (!host) return;
  const card = host.closest('.chart-card');
  if (card) card.remove();
}

// Compute numeric width and embed; retry while the container is 0px wide
async function embedWithWidth(el, spec, maxTries = 12, delayMs = 80) {
  const host = typeof el === "string" ? document.querySelector(el) : el;
  if (!host) return Promise.reject(new Error("mount element not found"));

  host.style.width = host.style.width || "100%";
  host.style.minHeight = host.style.minHeight || ((spec && spec.height) ? (spec.height + 16) + "px" : "236px");

  const theme = themeConfigFromCSS();

  for (let i = 0; i < maxTries; i++) {
    const w = host.clientWidth || (host.parentElement && host.parentElement.clientWidth) || 0;
    if (w > 0) {
      const s = withAutosize(spec, w, theme);
      return window.vegaEmbed(host, s, { actions: false, renderer: 'canvas' })
        .then(res => { try { res.view.resize(); } catch {} return res; });
    }
    await new Promise(r => setTimeout(r, delayMs));
  }

  const s = withAutosize(spec, 640, theme);
  return window.vegaEmbed(host, s, { actions: false, renderer: 'canvas' })
    .then(res => { try { res.view.resize(); } catch {} return res; });
}

function withAutosize(spec, width, theme) {
  const s = Object.assign({}, spec);
  s.width = Math.max(220, Math.floor(width));
  s.config = Object.assign({}, theme || {}, s.config || {});
  if (!s.autosize) s.autosize = { type: "fit", contains: "padding" };
  return s;
}

function VL_SAFE(spec, el) {
  const values = spec?.data?.values;
  if (!Array.isArray(values) || values.length === 0) {
    removeCardFor(el);
    return;
  }
  embedWithWidth(el, spec).catch(e => showError("vegaEmbed failed: " + (e?.message || e)));
}

function fallbackLine(name, value, totalMs) {
  const t = Math.max(1, Number(totalMs) || 1);
  const v = Number(value) || 0;
  return [{ name, time_ms: 0, value: v }, { name, time_ms: t, value: v }];
}



function charts(current, baseline){
  const curSeries  = Array.isArray(current.series) ? current.series : [];
  const baseSeries = baseline && Array.isArray(baseline.series) ? baseline.series : [];

  // Throughput over time (no inner title; stronger contrast; points)
  const seriesA = curSeries.length ? curSeries.map(p => ({ name:'After',  time_ms:p.time_ms, value:p.mb_s })) :
                                     fallbackLine('After',  current.throughput_mb_s, current.wall_time_ms);
  const seriesB = baseSeries.length ? baseSeries.map(p => ({ name:'Before', time_ms:p.time_ms, value:p.mb_s })) :
                                     (baseline ? fallbackLine('Before', baseline.throughput_mb_s, baseline.wall_time_ms) : []);
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 220,
    data: { values: [...seriesB, ...seriesA] },
    mark: { type: "line", interpolate: "monotone", point: { filled: true } },
    encoding: {
      x: { field: "time_ms", type: "quantitative", title: "Time (ms)", scale: { nice: true }},
      y: { field: "value", type: "quantitative", title: "MB/s", scale: { zero: false, nice: true } },
      color: { field: "name", type: "nominal", title: "name" }
    }
  }, "#chart-throughput");

  // RSS over time
  const rssA = curSeries.length ? curSeries.map(p => ({ name:'After',  time_ms:p.time_ms, value:p.rss_mb })) :
                                  fallbackLine('After',  current.peak_rss_mb, current.wall_time_ms);
  const rssB = baseSeries.length ? baseSeries.map(p => ({ name:'Before', time_ms:p.time_ms, value:p.rss_mb })) :
                                  (baseline ? fallbackLine('Before', baseline.peak_rss_mb, baseline.wall_time_ms) : []);
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 220,
    data: { values: [...rssB, ...rssA] },
    mark: { type: "line", interpolate: "monotone", point: { filled: true } },
    encoding: {
      x: { field: "time_ms", type: "quantitative", title: "Time (ms)", scale: { nice: true } },
      y: { field: "value", type: "quantitative", title: "RSS (MB)", scale: { zero: false, nice: true } },
      color: { field: "name", type: "nominal", title: "name" }
    }
  }, "#chart-rss");

  // Allocations over time
  const allocA = curSeries.length ? curSeries.map(p => ({ name:'After',  time_ms:p.time_ms, value:p.allocs_per_sec })) :
                                    fallbackLine('After',  current.allocs_per_sec, current.wall_time_ms);
  const allocB = baseSeries.length ? baseSeries.map(p => ({ name:'Before', time_ms:p.time_ms, value:p.allocs_per_sec })) :
                                    (baseline ? fallbackLine('Before', baseline.allocs_per_sec, baseline.wall_time_ms) : []);
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 220,
    data: { values: [...allocB, ...allocA] },
    mark: { type: "line", interpolate: "monotone", point: { filled: true } },
    encoding: {
      x: { field: "time_ms", type: "quantitative", title: "Time (ms)", scale: { nice: true } },
      y: { field: "value", type: "quantitative", title: "Allocs/sec", scale: { zero: false, nice: true } },
      color: { field: "name", type: "nominal", title: "name" }
    }
  }, "#chart-allocs");

  // Per-stage latency (bars) — fall back to total when empty
  const stA = asStageRows(current);
  const stB = baseline ? asStageRows(baseline) : [];
  const stageValues = (stB.length || stA.length)
    ? [...stB.map(s => ({name:'Before', ...s})), ...stA.map(s => ({name:'After', ...s}))]
    : [{name:'After', stage:'Total', duration_ms:Number(current.wall_time_ms)||0}]
        .concat(baseline ? [{name:'Before', stage:'Total', duration_ms:Number(baseline.wall_time_ms)||0}] : []);
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 260,
    data: { values: stageValues },
    mark: "bar",
    encoding: {
      y: { field: "stage", type: "nominal", sort: "-x", title: "Stage" },
      x: { field: "duration_ms", type: "quantitative", title: "Duration (ms)", scale: { nice: true } },
      color: { field: "name", type: "nominal", title: "name" },
      tooltip: [{field:"duration_ms", type:"quantitative"}]
    }
  }, "#chart-stages");

  // Errors by field — remove if empty
  const errs = Object.entries(current.errors_by_field || {}).map(([field, cnt]) => ({
    field, value: Number(cnt) || 0
  }));
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 220,
    data: { values: errs },
    mark: "bar",
    encoding: {
      x: { field: "field", type: "nominal", sort: "-y", title: "Field" },
      y: { field: "value", type: "quantitative", title: "Errors", scale: { nice: true } },
      tooltip: [{field:"value", type:"quantitative", title:"Errors"}]
    }
  }, "#chart-errors");

  // CSV vs JSONL tokens/sec — remove if no rows
  const fmtRows = (current.csv_vs_jsonl_tokens || []).map(r => ({
    format: r.format, tokens: r.tokens_per_sec
  }));
  VL_SAFE({
    $schema: "https://vega.github.io/schema/vega-lite/v5.json",
    height: 200,
    data: { values: fmtRows },
    mark: "bar",
    encoding: {
      x: { field: "format", type: "nominal", title: "Format" },
      y: { field: "tokens", type: "quantitative", title: "Tokens/sec", scale: { nice: true } },
      tooltip: [{field:"tokens", type:"quantitative"}]
    }
  }, "#chart-format");

  // Compare bars (only if baseline exists)
  if (baseline) {
    const mkCompareBar = (id, label, cur, base) => {
      const rows = [{ name:"Before", value:Number(base)||0 }, { name:"After", value:Number(cur)||0 }];
      VL_SAFE({
        $schema: "https://vega.github.io/schema/vega-lite/v5.json",
        height: 180,
        data: { values: rows },
        mark: "bar",
        encoding: {
          x:{ field:"name", type:"nominal" },
          y:{ field:"value", type:"quantitative", title:label, scale: { nice: true } }
        }
      }, id);
    };
    mkCompareBar("#chart-compare-tokens", "Tokens/sec",    current.tokens_per_sec,  baseline.tokens_per_sec);
    mkCompareBar("#chart-compare-mbs",    "MB/s",          current.throughput_mb_s, baseline.throughput_mb_s);
    mkCompareBar("#chart-compare-lat",    "p95 (ms)",      current.p95_ms,          baseline.p95_ms);
    mkCompareBar("#chart-compare-rss",    "Peak RSS (MB)", current.peak_rss_mb,     baseline.peak_rss_mb);
  } else {
    // no baseline → remove entire comparison panel & TOC link
    const comparisonPanel = document.getElementById('comparison');
    const comparisonLink  = document.querySelector('.toc a[href="#comparison"]');
    if (comparisonPanel) comparisonPanel.remove();
    if (comparisonLink)  comparisonLink.remove();
  }
}



function tables(current){
  const meta = [
    ["Filename", current.filename || "—"],
    ["Content-Type", current.content_type || "—"],
    ["Size (bytes)", fmt.int(current.file_size)],
    ["ETag", current.etag || "—"]
  ];
  const metaT = document.querySelector('#tbl-input-meta tbody');
  if (metaT) metaT.innerHTML = meta.map(([k,v]) => `<tr><td>${k}</td><td>${v}</td></tr>`).join('');

  const total = Number(current.wall_time_ms) || 0;
  let stageRows = (current.stage_times || []).map(s => {
    const pct = total > 0 ? (Number(s.duration_ms || 0) / total) * 100 : 0;
    return `<tr><td>${s.stage}</td><td class="num">${fmt.ms(s.duration_ms)}</td><td class="num">${fmt.num(pct,1)}%</td></tr>`;
  }).join('');
  if (!stageRows && total > 0) {
    stageRows = `<tr><td>Total</td><td class="num">${fmt.ms(total)}</td><td class="num">100%</td></tr>`;
  }
  const stageT = document.querySelector('#tbl-stage tbody');
  if (stageT) stageT.innerHTML = stageRows || `<tr><td colspan="3" class="muted">No stage timings.</td></tr>`;

  const errsMap = current.errors_by_field || {};
  const errRows = Object.keys(errsMap).length
    ? Object.entries(errsMap).map(([f,c]) => `<tr><td>${f}</td><td class="num">${fmt.int(c)}</td></tr>`).join('')
    : `<tr><td colspan="2" class="muted">No errors recorded.</td></tr>`;
  const errsT = document.querySelector('#tbl-errors tbody');
  if (errsT) errsT.innerHTML = errRows;

  let fmtTbl = (current.csv_vs_jsonl_tokens || []).map(r =>
    `<tr><td>${r.format}</td><td class="num">${fmt.num(r.tokens_per_sec)}</td><td class="num">${fmt.num(r.mb_s)}</td></tr>`
  ).join('');
  if (!fmtTbl) {
    const observed = current.content_type === "application/x-ndjson" ? "JSONL" :
                     current.content_type === "text/csv" ? "CSV" : "Observed";
    fmtTbl = `<tr><td>${observed}</td><td class="num">${fmt.num(current.tokens_per_sec)}</td><td class="num">${fmt.num(current.throughput_mb_s)}</td></tr>`;
  }
  const fmtT = document.querySelector('#tbl-format tbody');
  if (fmtT) fmtT.innerHTML = fmtTbl;

  try {
    const blob = new Blob([JSON.stringify(current, null, 2)], {type: "application/json"});
    const a = document.getElementById('download-json');
    if (a) a.href = URL.createObjectURL(blob);
  } catch {}
}



function vegaReady(){
  if (!window.vegaLite && window.vl) window.vegaLite = window.vl;
  return !!(window.vegaEmbed && window.vega && (window.vegaLite || window.vl));
}

function redrawCharts(state){
  const ids = [
    "#chart-throughput","#chart-rss","#chart-allocs","#chart-stages",
    "#chart-errors","#chart-format",
    "#chart-compare-tokens","#chart-compare-mbs","#chart-compare-lat","#chart-compare-rss"
  ];
  ids.forEach(sel => { const el = document.querySelector(sel); if (el) el.innerHTML = ""; });
  charts(state.current, state.baseline);
}

async function boot(){
  const ctx = await getCtx();
  const current  = ctx?.compare?.after  ?? ctx ?? {};
  const baseline = ctx?.compare?.before ?? null;

  // Theme toggle
  const root = document.documentElement;
  const stored = localStorage.getItem('ts-theme');
  if (stored) root.setAttribute('data-theme', stored);
  const state = { current, baseline };
  const toggle = document.querySelector('#toggle-theme');
  if (toggle) toggle.addEventListener('click', () => {
    const cur = root.getAttribute('data-theme') === 'light' ? 'dark' : 'light';
    root.setAttribute('data-theme', cur);
    localStorage.setItem('ts-theme', cur);
    // repaint charts with new theme
    if (vegaReady()) redrawCharts(state);
  });

  try { mountKPIs(current, baseline); } catch(e){ showError("KPIs failed: " + (e?.message||e)); }
  try { tables(current); }             catch(e){ showError("Tables failed: " + (e?.message||e)); }

  // Wait for Vega libs
  let tries = 0;
  while (!vegaReady() && tries < 40) { await new Promise(r => setTimeout(r, 100)); tries++; }

  console.log("[report] Vega globals:", {
    hasVega: !!window.vega,
    hasVL_vegaLite: !!window.vegaLite,
    hasVL_vl: !!window.vl,
    hasEmbed: !!window.vegaEmbed,
    vegaVersion: window.vega && window.vega.version,
    vegaLiteVersion: (window.vegaLite && window.vegaLite.version) || (window.vl && window.vl.version)
  });

  if (!vegaReady()) { showError("Vega libraries not loaded — charts skipped"); return; }

  try { charts(current, baseline); } catch(e){ showError("Charts failed: " + (e?.message||e)); }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", boot, { once: true });
} else {
  boot();
}
