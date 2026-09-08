const stage = document.getElementById("stage");
const design = document.getElementById("design");
const card = document.getElementById("card");
const jacketImg = document.getElementById("jacket-img");
const jacketFallback = document.getElementById("jacket-fallback");
const albumEl = document.getElementById("album");
const titleEl = document.getElementById("title");
const artistEl = document.getElementById("artist");
const durationEl = document.getElementById("duration");

const DESIGN_W = 1000;
const DESIGN_H = 250;
/** px/sec for overflow travel (excluding hold segments) */
const MARQUEE_PX_PER_SEC = 38;

let currentId = null;
let busy = false;
let pendingState = null;

function syncUiScale() {
  if (!design) return;
  const sx = window.innerWidth / DESIGN_W;
  const sy = window.innerHeight / DESIGN_H;
  const raw = Math.min(sx, sy);
  const snapped = Math.abs(raw - Math.round(raw)) < 0.08 ? Math.round(raw) : raw;
  const scale = Math.max(0.25, snapped);
  document.documentElement.style.setProperty("--lumia-ui-scale", String(scale));
}

function fmt(sec) {
  const s = Math.max(0, Math.floor(sec || 0));
  const m = Math.floor(s / 60);
  const r = String(s % 60).padStart(2, "0");
  return `${m}:${r}`;
}

function clearMarquee(el) {
  el.classList.remove("is-marquee");
  el.style.removeProperty("--marquee-shift");
  el.style.removeProperty("--marquee-duration");
}

function fillChars(el, text, startIndex) {
  clearMarquee(el);
  el.replaceChildren();
  const track = document.createElement("div");
  track.className = "lumia-line-track";
  const chars = Array.from(text);
  chars.forEach((ch, i) => {
    const span = document.createElement("span");
    span.className = "lumia-char";
    span.style.setProperty("--i", String(startIndex + i));
    span.textContent = ch === " " ? "\u00A0" : ch;
    track.appendChild(span);
  });
  el.appendChild(track);
  return startIndex + chars.length;
}

function setOptionalChars(el, value, startIndex) {
  const text = typeof value === "string" ? value.trim() : "";
  if (!text) {
    el.hidden = true;
    clearMarquee(el);
    el.replaceChildren();
    return startIndex;
  }
  el.hidden = false;
  return fillChars(el, text, startIndex);
}

function setupMarquees() {
  for (const el of [albumEl, titleEl, artistEl]) {
    if (!el || el.hidden) {
      if (el) clearMarquee(el);
      continue;
    }
    const track = el.querySelector(".lumia-line-track");
    if (!track) {
      clearMarquee(el);
      continue;
    }

    clearMarquee(el);
    void el.offsetWidth;
    const overflow = Math.ceil(track.scrollWidth - el.clientWidth);
    if (overflow <= 2) continue;

    const moveSec = Math.max(2.5, overflow / MARQUEE_PX_PER_SEC);
    const duration = moveSec / 0.34;
    el.style.setProperty("--marquee-shift", `-${overflow}px`);
    el.style.setProperty("--marquee-duration", `${duration.toFixed(2)}s`);
    el.classList.add("is-marquee");
  }
}

function applyContent(state) {
  const track = state.track;
  let index = 0;
  index = setOptionalChars(albumEl, track.album, index);
  index = fillChars(titleEl, track.title || "", index);
  index = setOptionalChars(artistEl, track.artist, index);
  durationEl.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;

  if (track.coverUrl) {
    jacketImg.hidden = false;
    jacketFallback.hidden = true;
    if (jacketImg.getAttribute("src") !== track.coverUrl) {
      jacketImg.src = track.coverUrl;
      if (jacketImg.decode) {
        jacketImg.decode().catch(() => {});
      }
    }
  } else {
    jacketImg.hidden = true;
    jacketImg.removeAttribute("src");
    jacketFallback.hidden = false;
  }

  return index;
}

function applyProgressOnly(state) {
  durationEl.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;
}

function runExitEnter(nextState) {
  stage.hidden = false;
  syncUiScale();
  applyContent(nextState);
  currentId = nextState.track.id;
  card.classList.remove("is-exit", "is-enter");
  setupMarquees();
}

function runHide() {
  if (currentId == null) {
    stage.hidden = true;
    return;
  }

  card.classList.remove("is-exit", "is-enter");
  stage.hidden = true;
  currentId = null;
  durationEl.textContent = "0:00 / 0:00";
  clearMarquee(albumEl);
  clearMarquee(titleEl);
  clearMarquee(artistEl);
  albumEl.hidden = true;
  albumEl.replaceChildren();
  titleEl.replaceChildren();
  artistEl.hidden = true;
  artistEl.replaceChildren();
}

async function flush() {
  if (busy) return;
  busy = true;

  try {
    while (pendingState !== null) {
      const state = pendingState;
      pendingState = null;

      const track = state.track;
      if (!track) {
        runHide();
        continue;
      }

      if (currentId === track.id) {
        stage.hidden = false;
        applyProgressOnly(state);
        continue;
      }

      runExitEnter(state);
    }
  } finally {
    busy = false;
    if (pendingState !== null) flush();
  }
}

function applyState(state) {
  pendingState = state;
  flush();
}

async function poll() {
  try {
    const res = await fetch("/api/state");
    const state = await res.json();
    applyState(state);
  } catch {
    /* server not ready */
  }
}

window.addEventListener("resize", () => {
  syncUiScale();
  if (currentId != null) setupMarquees();
});

document.body.dataset.anim = "none";
syncUiScale();
poll();
setInterval(poll, 250);
