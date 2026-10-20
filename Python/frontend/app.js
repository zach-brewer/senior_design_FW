// Acoustic Shot Locator — frontend
// Talks to the Flask/SocketIO backend over REST (initial state) + WebSocket
// (live updates), renders a target-shaped, interactive Plotly chart, and
// keeps the per-mic "last timestamp received" readout current.

(function () {
  "use strict";

  // ---------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------
  let cfg = null;                 // /api/config response
  let micIds = [];                // [1,2,3,4,5,6]
  let micRowOf = {};               // mic_id -> row index (1 = front row, 2 = back row, ...)
  let events = [];                // shot history, ascending by id
  let groups = [];                 // closed shot groups, ascending by id
  let selectedEventId = null;
  let selectedGroupId = null;      // when set, non-member shots/groups are dimmed
  let pickMode = false;

  let armed = false;
  let currentGroupId = null;
  let currentGroupShotCount = 0;

  // Which target background image (if any) is showing -- purely a display
  // choice, keyed into cfg.target_types. Does not affect scoring, which
  // always uses cfg.ring_radii_m / cfg.target_center_m regardless.
  let currentTargetType = "rings";

  const micLocal = {};             // mic_id -> { delta_us, receivedAtMs } (liveness only)
  const micLastShotMs = {};        // mic_id -> relative ms from the most recent shot event
  let lastShotTriggerMicId = null; // which mic was "first" on the most recent shot

  const css = getComputedStyle(document.documentElement);
  const cssVar = (name) => css.getPropertyValue(name).trim();

  // ---------------------------------------------------------------------
  // Utilities
  // ---------------------------------------------------------------------
  function hexToRgb(hex) {
    const h = hex.replace("#", "");
    return [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16)];
  }
  function rgbToHex([r, g, b]) {
    return "#" + [r, g, b].map((v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, "0")).join("");
  }
  function lerpColor(hexA, hexB, t) {
    const a = hexToRgb(hexA), b = hexToRgb(hexB);
    return rgbToHex([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]);
  }
  function fmtAge(sec) {
    if (sec === null || sec === undefined) return "never";
    if (sec < 1) return "just now";
    if (sec < 60) return sec.toFixed(1) + "s ago";
    return Math.floor(sec / 60) + "m ago";
  }
  function fmtM(v) { return Number(v).toFixed(3) + " m"; }
  function fmtMm(m) { return (Number(m) * 1000).toFixed(1) + " mm"; }
  // Relative-to-trigger display, in milliseconds. Every shot's reference is
  // whichever mic saw it first, so these are always >= 0 by construction
  // (a tiny negative float, if it ever shows up, is clamped to zero rather
  // than printed as a confusing "-0.000").
  function fmtRelMs(ms) {
    if (ms === null || ms === undefined || Number.isNaN(ms)) return "—";
    return Math.max(0, Number(ms)).toFixed(3) + " ms";
  }

  // Compute row grouping from the mic geometry: mics sharing a y coordinate
  // belong to the same physical row. Works for any number of rows, not just 2.
  function computeMicRows() {
    const ys = [...new Set(micIds.map((id) => cfg.mic_positions_m[id][1]))].sort((a, b) => b - a);
    micIds.forEach((id) => {
      micRowOf[id] = ys.indexOf(cfg.mic_positions_m[id][1]) + 1; // 1 = closest to target
    });
  }

  // ---------------------------------------------------------------------
  // Arm / disarm
  // ---------------------------------------------------------------------
  function updateArmUi() {
    const bar = document.getElementById("armBar");
    const text = document.getElementById("armText");
    const dot = document.getElementById("armDot");
    const btn = document.getElementById("btnArm");

    bar.classList.toggle("is-armed", armed);
    dot.className = "status-dot " + (armed ? "live" : "idle");
    text.textContent = armed
      ? "Armed — group #" + currentGroupId + ", " + currentGroupShotCount + " shot" + (currentGroupShotCount === 1 ? "" : "s")
      : "Idle — not receiving";
    btn.textContent = armed ? "Disarm system" : "Arm system";

    document.getElementById("btnSimulate").disabled = !armed;
    document.getElementById("btnPick").disabled = !armed;
    if (!armed && pickMode) setPickMode(false);
  }

  async function toggleArm() {
    const res = await fetch(armed ? "/api/disarm" : "/api/arm", { method: "POST" });
    if (!res.ok) return; // 409 (already in that state, e.g. a race) -- next status push will reconcile
  }

  // ---------------------------------------------------------------------
  // Mic status cards
  // ---------------------------------------------------------------------
  function buildMicCards() {
    const row = document.getElementById("micRow");
    row.innerHTML = "";
    micIds.forEach((id) => {
      const pos = cfg.mic_positions_m[id];
      const card = document.createElement("div");
      card.className = "mic-card";
      card.id = "micCard" + id;
      card.innerHTML =
        '<div class="mic-card-top">' +
          '<span class="mic-card-name"><span class="status-dot idle" id="micDot' + id + '"></span>Mic ' + id +
            '<span class="trigger-badge hidden" id="triggerBadge' + id + '">TRIGGER</span></span>' +
          '<span class="mic-card-pos">row ' + micRowOf[id] + ' &middot; (' + pos[0].toFixed(1) + ", " + pos[1].toFixed(3) + ") m</span>" +
        "</div>" +
        '<div class="mic-card-readout"><div class="mic-card-time" id="micTime' + id + '">—</div></div>' +
        '<div class="mic-card-age" id="micAge' + id + '">never</div>';
      row.appendChild(card);
    });
  }

  // Store a raw reading (delta-since-trigger) without rendering; callers
  // re-render afterwards. This only drives the liveness dot/age text now --
  // the big number comes from the most recent completed shot (see
  // applyShotToMicCards).
  function storeMicReading(micId, deltaUs, receivedAtMs) {
    micLocal[micId] = { delta_us: deltaUs, receivedAtMs };
  }

  function renderMicCard(micId) {
    const info = micLocal[micId];
    const timeEl = document.getElementById("micTime" + micId);
    const ageEl = document.getElementById("micAge" + micId);
    const dotEl = document.getElementById("micDot" + micId);
    if (!timeEl) return;

    timeEl.textContent = micLastShotMs[micId] !== undefined ? fmtRelMs(micLastShotMs[micId]) : "—";

    if (!info || info.delta_us == null) {
      ageEl.textContent = "never";
      dotEl.className = "status-dot idle";
      return;
    }
    const ageSec = (Date.now() - info.receivedAtMs) / 1000;
    ageEl.textContent = fmtAge(ageSec);
    dotEl.className = "status-dot " + (ageSec < 3 ? "live" : "idle");
  }

  function renderAllMicCards() { micIds.forEach(renderMicCard); }

  // Whichever mic was first to see the latest shot gets the TRIGGER badge --
  // this moves around from shot to shot, it isn't a fixed mic.
  function applyShotToMicCards(ev) {
    micIds.forEach((id) => { micLastShotMs[id] = ev.relative_to_trigger_ms[String(id)]; });
    lastShotTriggerMicId = ev.reference_mic_id;
    micIds.forEach((id) => {
      document.getElementById("triggerBadge" + id).classList.toggle("hidden", id !== lastShotTriggerMicId);
      document.getElementById("micCard" + id).classList.toggle("mic-card-trigger", id === lastShotTriggerMicId);
    });
    renderAllMicCards();
  }

  setInterval(renderAllMicCards, 500);

  // ---------------------------------------------------------------------
  // Chart
  // ---------------------------------------------------------------------
  let gd; // the Plotly graph div

  // Looks up the currently-selected target type's config entry (label,
  // image_url, width_m/height_m), or null for the plain "rings" mode or
  // before cfg has loaded.
  function currentTargetTypeInfo() {
    return (cfg && cfg.target_types && cfg.target_types[currentTargetType]) || null;
  }

  // Plotly `layout.images` entries for the current target type -- empty
  // when "rings only" is selected. The image is centered on
  // target_center_m and sized to its real physical dimensions (so it lines
  // up correctly against the mic geometry), drawn below the traces/shapes.
  function targetImages() {
    const tt = currentTargetTypeInfo();
    if (!tt || !tt.image_url) return [];
    const [cx, cy] = cfg.target_center_m;
    const halfW = tt.width_m / 2;
    const halfH = tt.height_m / 2;
    return [{
      source: tt.image_url,
      xref: "x", yref: "y",
      x: cx - halfW, y: cy + halfH,
      sizex: tt.width_m, sizey: tt.height_m,
      xanchor: "left", yanchor: "top",
      sizing: "stretch",
      layer: "below",
    }];
  }

  function ringShapes() {
    const [cx, cy] = cfg.target_center_m;
    const radii = cfg.ring_radii_m;
    const shapes = [];
    // Scoring itself never changes with the target type (see
    // currentTargetTypeInfo's comment), but how the rings are *drawn* does:
    // with a target photo behind them, opaque banded discs would completely
    // hide it, so they become thin dotted outlines instead, drawn above
    // everything so they stay legible over the photo. With no photo, it's
    // the original opaque banded-disc look.
    const tt = currentTargetTypeInfo();
    const imageActive = !!(tt && tt.image_url);
    for (let i = radii.length - 1; i >= 0; i--) {
      const r = radii[i];
      if (imageActive) {
        shapes.push({
          type: "circle", xref: "x", yref: "y",
          x0: cx - r, x1: cx + r, y0: cy - r, y1: cy + r,
          line: { color: cssVar("--text-muted"), width: 1, dash: "dot" },
          fillcolor: "transparent",
          layer: "above",
        });
      } else {
        // Each ring is drawn as a full filled disc, not an annulus, so they
        // must be layered largest-first -- otherwise the last (biggest, most
        // likely opaque) disc paints over every smaller one drawn before it.
        // Drawing outside-in makes each smaller disc appear "cut into" the
        // larger one beneath it, which is what gives the banded-ring look.
        const band = i % 2 === 0 ? cssVar("--surface") : cssVar("--surface-2");
        shapes.push({
          type: "circle", xref: "x", yref: "y",
          x0: cx - r, x1: cx + r, y0: cy - r, y1: cy + r,
          line: { color: cssVar("--baseline"), width: 1 },
          fillcolor: band,
          layer: "below",
        });
      }
    }
    // crosshair at the exact center
    const armLen = radii[0] * 0.9;
    shapes.push({ type: "line", x0: cx - armLen, x1: cx + armLen, y0: cy, y1: cy, line: { color: cssVar("--text-muted"), width: 1 } });
    shapes.push({ type: "line", x0: cx, x1: cx, y0: cy - armLen, y1: cy + armLen, line: { color: cssVar("--text-muted"), width: 1 } });
    // one dotted mounting line per physical mic row
    const xs = micIds.map((id) => cfg.mic_positions_m[id][0]);
    const rowYs = [...new Set(micIds.map((id) => cfg.mic_positions_m[id][1]))];
    rowYs.forEach((rowY) => {
      shapes.push({
        type: "line", x0: Math.min(...xs) - 0.5, x1: Math.max(...xs) + 0.5, y0: rowY, y1: rowY,
        line: { color: cssVar("--baseline"), width: 1, dash: "dot" }, layer: "below",
      });
    });
    return shapes;
  }

  // One circle per closed shot group. With no group selected they're all
  // drawn at a modest, even weight; selecting one in the Groups panel makes
  // that circle bold and fades the rest almost away, echoing the same
  // dimming applied to the shot markers themselves.
  function groupShapes() {
    return groups.map((g) => {
      const isSelected = g.id === selectedGroupId;
      const dimmed = selectedGroupId !== null && !isSelected;
      return {
        type: "circle", xref: "x", yref: "y",
        x0: g.center_x_m - g.radius_m, x1: g.center_x_m + g.radius_m,
        y0: g.center_y_m - g.radius_m, y1: g.center_y_m + g.radius_m,
        line: {
          color: isSelected ? cssVar("--series-shot") : cssVar("--text-muted"),
          width: isSelected ? 2.5 : 1.25,
          dash: isSelected ? "solid" : "dot",
        },
        fillcolor: "transparent",
        opacity: dimmed ? 0.12 : (isSelected ? 1 : 0.6),
        layer: "above",
      };
    });
  }

  function redrawShapes() {
    Plotly.relayout("chart", { shapes: [...ringShapes(), ...groupShapes()] });
  }

  function axisRanges() {
    const [cx, cy] = cfg.target_center_m;
    const maxR = cfg.ring_radii_m[cfg.ring_radii_m.length - 1];
    const xs = micIds.map((id) => cfg.mic_positions_m[id][0]);
    const ys = micIds.map((id) => cfg.mic_positions_m[id][1]);
    let xMin = Math.min(cx - maxR * 1.2, Math.min(...xs) - 0.4);
    let xMax = Math.max(cx + maxR * 1.2, Math.max(...xs) + 0.4);
    let yMin = Math.min(-0.4, Math.min(...ys) - 0.35);
    let yMax = cy + maxR * 1.2;

    // A target photo is usually bigger than the generic ring set (a full
    // 23x35" sheet vs. a ~0.6 m max ring radius), so make sure the whole
    // sheet fits on screen rather than getting cropped to the smaller
    // ring-based range.
    const tt = currentTargetTypeInfo();
    if (tt && tt.image_url) {
      const halfW = tt.width_m / 2 + 0.05;
      const halfH = tt.height_m / 2 + 0.05;
      xMin = Math.min(xMin, cx - halfW);
      xMax = Math.max(xMax, cx + halfW);
      yMin = Math.min(yMin, cy - halfH);
      yMax = Math.max(yMax, cy + halfH);
    }
    return { x: [xMin, xMax], y: [yMin, yMax] };
  }

  // Swaps the background target image (or back to plain rings), refits the
  // view to the new target's extent, and redraws the ring overlay in the
  // style appropriate for whether a photo is now showing. Scoring itself
  // never changes -- see currentTargetTypeInfo's comment.
  function setTargetType(key) {
    if (!cfg.target_types[key]) return;
    currentTargetType = key;
    const range = axisRanges();
    Plotly.relayout("chart", {
      images: targetImages(),
      shapes: [...ringShapes(), ...groupShapes()],
      "xaxis.range": range.x,
      "yaxis.range": range.y,
    });
  }

  function populateTargetTypeSelect() {
    const sel = document.getElementById("targetTypeSelect");
    sel.innerHTML = "";
    // cfg.target_type_order pins the display order -- cfg.target_types'
    // own key order isn't reliable (JSON object key order isn't guaranteed,
    // and this backend's jsonify sorts keys alphabetically anyway).
    const order = cfg.target_type_order || Object.keys(cfg.target_types);
    order.forEach((key) => {
      if (!cfg.target_types[key]) return;
      const opt = document.createElement("option");
      opt.value = key;
      opt.textContent = cfg.target_types[key].label;
      sel.appendChild(opt);
    });
    sel.value = currentTargetType;
  }

  function micTrace() {
    const xs = micIds.map((id) => cfg.mic_positions_m[id][0]);
    const ys = micIds.map((id) => cfg.mic_positions_m[id][1]);
    // Front row's labels go above their markers (toward the target) and
    // back row's go below, so the two rows' text doesn't collide in the
    // narrow gap between them.
    const textposition = micIds.map((id) => (micRowOf[id] === 1 ? "top center" : "bottom center"));
    return {
      x: xs, y: ys, mode: "markers+text",
      type: "scatter",
      marker: { symbol: "square", size: 14, color: cssVar("--series-mic"), line: { width: 1, color: cssVar("--border-strong") } },
      text: micIds.map((id) => "Mic " + id),
      textposition,
      textfont: { size: 11, color: cssVar("--text-secondary") },
      name: "Microphones",
      hovertemplate: "%{text}<br>x = %{x:.2f} m, y = %{y:.3f} m<extra></extra>",
    };
  }

  function shotTrace() {
    const n = events.length;
    const xs = [], ys = [], colors = [], sizes = [], lineWidths = [], lineColors = [], opacities = [], customdata = [];
    events.forEach((ev, i) => {
      xs.push(ev.x_m); ys.push(ev.y_m);
      const t = n <= 1 ? 1 : i / (n - 1);
      colors.push(lerpColor(cssVar("--shot-light"), cssVar("--shot-dark"), t));
      const isSelected = ev.id === selectedEventId;
      sizes.push(isSelected ? 15 : 10);
      lineWidths.push(isSelected ? 2.5 : 1);
      lineColors.push(isSelected ? cssVar("--text-primary") : cssVar("--border-strong"));
      // The core of the "select a group" feature: everything outside the
      // selected group fades to near-invisible instead of being hidden
      // outright, so the overall pattern of shots is still visible for
      // context while one group is the clear focus.
      opacities.push(selectedGroupId !== null && ev.group_id !== selectedGroupId ? 0.12 : 1.0);
      customdata.push(ev);
    });
    return {
      x: xs, y: ys, mode: "markers", type: "scatter",
      marker: { size: sizes, color: colors, opacity: opacities, line: { width: lineWidths, color: lineColors } },
      name: "Shots",
      customdata,
      hovertemplate:
        "Shot #%{customdata.id} (group %{customdata.group_id})<br>score %{customdata.score}<br>x = %{x:.3f} m, y = %{y:.3f} m<extra></extra>",
    };
  }

  function initChart() {
    const range = axisRanges();
    const layout = {
      shapes: [...ringShapes(), ...groupShapes()],
      images: targetImages(),
      xaxis: { range: range.x, zeroline: false, gridcolor: cssVar("--grid"), color: cssVar("--text-muted"), title: "x (m)" },
      yaxis: { range: range.y, zeroline: false, gridcolor: cssVar("--grid"), color: cssVar("--text-muted"), title: "y (m)", scaleanchor: "x", scaleratio: 1 },
      margin: { l: 48, r: 16, t: 8, b: 40 },
      plot_bgcolor: cssVar("--surface"),
      paper_bgcolor: cssVar("--surface"),
      font: { family: "system-ui, -apple-system, Segoe UI, sans-serif", color: cssVar("--text-secondary"), size: 12 },
      hovermode: "closest",
      showlegend: true,
      legend: { orientation: "h", y: 1.06, x: 0, font: { size: 11 } },
      dragmode: "pan",
    };
    Plotly.newPlot("chart", [shotTrace(), micTrace()], layout, {
      displaylogo: false,
      responsive: true,
      scrollZoom: true,
      modeBarButtonsToRemove: ["lasso2d", "select2d"],
    });
    gd = document.getElementById("chart");

    gd.on("plotly_click", (data) => {
      if (pickMode) return; // handled by the native listener below
      const p = data.points && data.points[0];
      if (p && p.data.name === "Shots" && p.customdata) selectEvent(p.customdata.id);
    });

    gd.addEventListener("click", onChartNativeClick);
  }

  function redrawShots() {
    Plotly.restyle("chart", {
      x: [shotTrace().x], y: [shotTrace().y],
      marker: [shotTrace().marker], customdata: [shotTrace().customdata],
    }, [0]);
  }

  // Native click handler used only for "pick a point" placement, since
  // Plotly's own click event only fires when a data point is hit.
  function onChartNativeClick(evt) {
    if (!pickMode) return;
    try {
      const fl = gd._fullLayout;
      const rect = gd.getBoundingClientRect();
      const xPix = evt.clientX - rect.left;
      const yPix = evt.clientY - rect.top;
      const size = fl._size;
      if (xPix < size.l || xPix > size.l + size.w || yPix < size.t || yPix > size.t + size.h) return;
      const x = fl.xaxis.p2l(xPix - size.l);
      const y = fl.yaxis.p2l(yPix - size.t);
      simulateShot({ x_m: x, y_m: y });
    } catch (e) {
      console.warn("Pick-to-place unavailable in this browser/Plotly version:", e);
    } finally {
      setPickMode(false);
    }
  }

  function setPickMode(on) {
    pickMode = on && armed;
    const btn = document.getElementById("btnPick");
    btn.setAttribute("aria-pressed", pickMode ? "true" : "false");
    btn.textContent = pickMode ? "Click anywhere on the target…" : "Click target to place shot";
    gd.style.cursor = pickMode ? "crosshair" : "";
  }

  // ---------------------------------------------------------------------
  // Shot log + detail panel
  // ---------------------------------------------------------------------
  function addLogRow(ev) {
    document.getElementById("logEmpty").style.display = "none";
    const tbody = document.getElementById("logTableBody");
    const tr = document.createElement("tr");
    tr.id = "logRow" + ev.id;
    tr.innerHTML =
      "<td class=\"num\">" + ev.id + "</td>" +
      "<td class=\"num\">" + ev.group_id + "</td>" +
      "<td class=\"num\">" + ev.score + "</td>" +
      "<td class=\"num\">" + ev.x_m.toFixed(3) + "</td>" +
      "<td class=\"num\">" + ev.y_m.toFixed(3) + "</td>" +
      "<td>" + new Date(ev.server_time_iso).toLocaleTimeString() + "</td>";
    tr.addEventListener("click", () => selectEvent(ev.id));
    tbody.insertBefore(tr, tbody.firstChild); // newest on top
  }

  function selectEvent(id) {
    selectedEventId = id;
    const ev = events.find((e) => e.id === id);
    document.querySelectorAll(".log-table tr.selected").forEach((r) => r.classList.remove("selected"));
    const row = document.getElementById("logRow" + id);
    if (row) row.classList.add("selected");
    redrawShots();
    if (!ev) return;

    document.getElementById("detailEmpty").classList.add("hidden");
    document.getElementById("detailContent").classList.remove("hidden");
    document.getElementById("detailScore").textContent = ev.score + " / " + cfg.ring_radii_m.length;
    document.getElementById("detailGroup").textContent = "#" + ev.group_id;
    document.getElementById("detailPos").textContent = "(" + ev.x_m.toFixed(3) + ", " + ev.y_m.toFixed(3) + ") m";
    document.getElementById("detailDist").textContent = fmtM(ev.distance_from_center_m);
    document.getElementById("detailResidual").textContent =
      fmtM(ev.residual_m) + (ev.converged ? "" : "  (fit did not fully converge)");

    const table = document.getElementById("detailMicTimes");
    table.innerHTML = "";
    micIds.forEach((id) => {
      const relMs = ev.relative_to_trigger_ms[String(id)];
      const isRef = id === ev.reference_mic_id;
      const tr = document.createElement("tr");
      tr.innerHTML =
        "<td>Mic " + id + (isRef ? " (trigger)" : "") + "</td>" +
        "<td>" + fmtRelMs(relMs) + "</td>";
      table.appendChild(tr);
    });
  }

  // ---------------------------------------------------------------------
  // Groups panel
  // ---------------------------------------------------------------------
  function addGroupRow(g) {
    document.getElementById("groupsEmpty").style.display = "none";
    const tbody = document.getElementById("groupsTableBody");
    const tr = document.createElement("tr");
    tr.id = "groupRow" + g.id;
    tr.innerHTML =
      "<td class=\"num\">" + g.id + "</td>" +
      "<td class=\"num\">" + g.shot_count + "</td>" +
      "<td class=\"num\">" + fmtMm(g.extreme_spread_m) + "</td>" +
      "<td>" + new Date(g.ended_at_iso).toLocaleTimeString() + "</td>";
    tr.addEventListener("click", () => selectGroup(g.id === selectedGroupId ? null : g.id));
    tbody.insertBefore(tr, tbody.firstChild); // newest on top
  }

  function selectGroup(id) {
    selectedGroupId = id;
    document.querySelectorAll(".log-table tr.group-selected").forEach((r) => r.classList.remove("group-selected"));
    if (id !== null) {
      const row = document.getElementById("groupRow" + id);
      if (row) row.classList.add("group-selected");
    }
    redrawShots();
    redrawShapes();
  }

  function clearLog() {
    events = [];
    groups = [];
    selectedEventId = null;
    selectedGroupId = null;
    document.getElementById("logTableBody").innerHTML = "";
    document.getElementById("logEmpty").style.display = "block";
    document.getElementById("groupsTableBody").innerHTML = "";
    document.getElementById("groupsEmpty").style.display = "block";
    document.getElementById("detailEmpty").classList.remove("hidden");
    document.getElementById("detailContent").classList.add("hidden");
    redrawShots();
    redrawShapes();
  }

  function exportCsv() {
    const rows = [["id", "group_id", "trigger_mic_id", "score", "x_m", "y_m", "distance_from_center_m", "residual_m", "server_time_iso"]
      .concat(micIds.map((id) => "mic" + id + "_relative_ms"))
      .concat(micIds.map((id) => "mic" + id + "_delta_us"))];
    events.forEach((ev) => {
      rows.push([
        ev.id, ev.group_id, ev.reference_mic_id, ev.score, ev.x_m, ev.y_m, ev.distance_from_center_m, ev.residual_m, ev.server_time_iso,
      ].concat(micIds.map((id) => ev.relative_to_trigger_ms[String(id)]))
       .concat(micIds.map((id) => ev.mic_times_us[String(id)])));
    });
    const csv = rows.map((r) => r.join(",")).join("\n");
    const blob = new Blob([csv], { type: "text/csv" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "shot_log.csv";
    a.click();
  }

  // ---------------------------------------------------------------------
  // Backend calls
  // ---------------------------------------------------------------------
  async function simulateShot(body) {
    await fetch("/api/simulate", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {}),
    });
  }

  // ---------------------------------------------------------------------
  // Settings (mic trigger threshold -> Nucleo)
  // ---------------------------------------------------------------------
  // This box does NOT reflect the Nucleo's actual live threshold -- nothing
  // is ever read back from it. It just remembers the last value *this*
  // website sent (or the configured default, before anything's been sent).
  function destHint() {
    return "Sends to " + cfg.nucleo_host + ":" + cfg.nucleo_port + ".";
  }

  function updateSettingsUi() {
    document.getElementById("voltageThresholdInput").value = cfg.voltage_threshold_v;
    document.getElementById("thresholdHint").textContent = destHint();
  }

  async function sendVoltageThreshold() {
    const input = document.getElementById("voltageThresholdInput");
    const value = parseFloat(input.value);
    const hint = document.getElementById("thresholdHint");
    if (Number.isNaN(value) || value < 0) return;

    let result;
    try {
      const res = await fetch("/api/nucleo/voltage-threshold", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ voltage_threshold_v: value }),
      });
      result = await res.json();
    } catch (e) {
      hint.textContent = "Could not reach the backend to send it.";
      return;
    }

    if (result.sent) {
      cfg.voltage_threshold_v = result.voltage_threshold_v;
      hint.textContent = "Sent " + value.toFixed(2) + " V to " + cfg.nucleo_host + ":" + cfg.nucleo_port + ".";
    } else {
      hint.textContent = "Failed to send (socket error) -- check NUCLEO_HOST/PORT in config.py.";
    }
    setTimeout(() => { hint.textContent = destHint(); }, 3000);
  }

  // ---------------------------------------------------------------------
  // Wiring
  // ---------------------------------------------------------------------
  function wireControls() {
    document.getElementById("btnArm").addEventListener("click", toggleArm);
    document.getElementById("btnSimulate").addEventListener("click", () => simulateShot({}));
    document.getElementById("btnClear").addEventListener("click", async () => {
      await fetch("/api/events", { method: "DELETE" });
      clearLog();
    });
    document.getElementById("btnPick").addEventListener("click", () => setPickMode(!pickMode));
    document.getElementById("btnExport").addEventListener("click", exportCsv);
    document.getElementById("btnShowAllGroups").addEventListener("click", () => selectGroup(null));
    document.getElementById("btnSendThreshold").addEventListener("click", sendVoltageThreshold);
    document.getElementById("voltageThresholdInput").addEventListener("keydown", (e) => {
      if (e.key === "Enter") sendVoltageThreshold();
    });
    document.getElementById("targetTypeSelect").addEventListener("change", (e) => {
      setTargetType(e.target.value);
    });
  }

  function wireSocket() {
    const socket = io();
    const dot = document.getElementById("connDot");
    const text = document.getElementById("connText");

    socket.on("connect", () => { dot.className = "status-dot live"; text.textContent = "Live"; });
    socket.on("disconnect", () => { dot.className = "status-dot down"; text.textContent = "Disconnected"; });
    socket.io.on("reconnect_attempt", () => { dot.className = "status-dot idle"; text.textContent = "Reconnecting…"; });

    socket.on("mic_reading", (msg) => {
      storeMicReading(msg.mic_id, msg.delta_us, Date.now());
      renderAllMicCards();
    });

    socket.on("shot_event", (ev) => {
      events.push(ev);
      addLogRow(ev);
      redrawShots();
      selectEvent(ev.id);
      applyShotToMicCards(ev);
      if (armed && ev.group_id === currentGroupId) {
        currentGroupShotCount += 1;
        updateArmUi();
      }
    });

    socket.on("armed_changed", (msg) => {
      armed = msg.armed;
      currentGroupId = msg.group_id;
      currentGroupShotCount = 0;
      updateArmUi();
    });

    socket.on("group_closed", (g) => {
      groups.push(g);
      addGroupRow(g);
      redrawShapes();
    });

    socket.on("events_cleared", () => clearLog());

    // Keeps the threshold box in sync if another browser tab/window sends a
    // new value -- purely a UI convenience, still never reflects the
    // Nucleo's actual live value (see updateSettingsUi's comment).
    socket.on("voltage_threshold_sent", (msg) => {
      cfg.voltage_threshold_v = msg.voltage_threshold_v;
      document.getElementById("voltageThresholdInput").value = msg.voltage_threshold_v;
    });
  }

  // ---------------------------------------------------------------------
  // Boot
  // ---------------------------------------------------------------------
  async function boot() {
    const [cfgResp, statusResp, groupsResp, micStatusResp, eventsResp] = await Promise.all([
      fetch("/api/config"), fetch("/api/status"), fetch("/api/groups"),
      fetch("/api/mics/status"), fetch("/api/events"),
    ]);
    cfg = await cfgResp.json();
    const status = await statusResp.json();
    const groupHistory = await groupsResp.json();
    const micStatus = await micStatusResp.json();
    const history = await eventsResp.json();

    micIds = Object.keys(cfg.mic_positions_m).map(Number).sort((a, b) => a - b);
    computeMicRows();
    document.getElementById("udpPortText").textContent = "UDP :" + cfg.udp_port;
    currentTargetType = (cfg.target_types && cfg.target_types[cfg.default_target_type])
      ? cfg.default_target_type : "rings";

    armed = status.armed;
    currentGroupId = status.current_group_id;
    currentGroupShotCount = status.current_group_shot_count;

    buildMicCards();
    populateTargetTypeSelect();
    initChart();
    wireControls();
    wireSocket();
    updateArmUi();
    updateSettingsUi();

    Object.values(micStatus).forEach((s) => {
      if (s.delta_us != null) storeMicReading(s.mic_id, s.delta_us, Date.now() - (s.age_s || 0) * 1000);
    });

    groups = groupHistory;
    groups.forEach(addGroupRow);

    events = history;
    events.forEach(addLogRow);
    if (events.length) applyShotToMicCards(events[events.length - 1]);
    renderAllMicCards();

    redrawShots();
    redrawShapes();
    if (events.length) selectEvent(events[events.length - 1].id);
  }

  document.addEventListener("DOMContentLoaded", boot);
})();
