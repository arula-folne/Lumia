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
const EXIT_MS = 420;
const JACKET_MS = 420;
const TEXT_START_MS = 300;
const CHAR_STAGGER_MS = 32;
const CHAR_DUR_MS = 280;
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

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function enterDurationMs(charCount) {
  if (charCount <= 0) return JACKET_MS;
  return TEXT_START_MS + (charCount - 1) * CHAR_STAGGER_MS + CHAR_DUR_MS;
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

function clearAnimClasses() {
  card.classList.remove("is-exit", "is-enter");
}

function restartEnterAnim() {
  clearAnimClasses();
  void card.offsetWidth;
  card.classList.add("is-enter");
}

function hasNewerTrack(thanId) {
  const t = pendingState && pendingState.track;
  return !!(t && t.id !== thanId);
}

async function setJacket(trackId, coverUrl) {
  if (!coverUrl) {
    jacketImg.hidden = true;
    jacketImg.removeAttribute("src");
    jacketImg.removeAttribute("data-track-id");
    jacketImg.style.visibility = "";
    jacketFallback.hidden = false;
    return;
  }

  const url = `${coverUrl}${coverUrl.includes("?") ? "&" : "?"}v=${encodeURIComponent(trackId)}`;
  if (jacketImg.getAttribute("data-track-id") === trackId && jacketImg.getAttribute("src") === url) {
    jacketImg.hidden = false;
    jacketFallback.hidden = true;
    jacketImg.style.visibility = "visible";
    return;
  }

  jacketFallback.hidden = true;
  jacketImg.hidden = false;
  jacketImg.style.visibility = "hidden";
  jacketImg.setAttribute("data-track-id", trackId);
  jacketImg.src = url;

  try {
    if (jacketImg.decode) await jacketImg.decode();
  } catch {
    /* keep hidden fallback if decode fails */
  }

  if (jacketImg.getAttribute("data-track-id") !== trackId) return;

  if (jacketImg.naturalWidth > 0) {
    jacketImg.style.visibility = "visible";
    jacketFallback.hidden = true;
  } else {
    jacketImg.hidden = true;
    jacketFallback.hidden = false;
  }
}

/** @returns {Promise<number>} total animated character count */
async function applyContent(state) {
  const track = state.track;
  let index = 0;
  index = setOptionalChars(albumEl, track.album, index);
  index = fillChars(titleEl, track.title || "", index);
  index = setOptionalChars(artistEl, track.artist, index);
  durationEl.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;
  await setJacket(track.id, track.coverUrl || null);
  return index;
}

function applyProgressOnly(state) {
  durationEl.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;
}

async function runExitEnter(nextState) {
  const targetId = nextState.track.id;
  const hadTrack = currentId != null;

  if (hadTrack) {
    clearAnimClasses();
    void card.offsetWidth;
    card.classList.add("is-exit");
    const step = 40;
    for (let t = 0; t < EXIT_MS; t += step) {
      if (hasNewerTrack(targetId)) break;
      await sleep(Math.min(step, EXIT_MS - t));
    }
  }

  /* Drop stale transition if a newer track arrived during exit */
  if (hasNewerTrack(targetId)) return;

  stage.hidden = false;
  syncUiScale();
  const charCount = await applyContent(nextState);
  if (hasNewerTrack(targetId)) return;

  currentId = targetId;
  restartEnterAnim();

  const total = enterDurationMs(charCount);
  const step = 40;
  for (let t = 0; t < total; t += step) {
    if (hasNewerTrack(targetId)) break;
    await sleep(Math.min(step, total - t));
  }

  card.classList.remove("is-enter");
  if (!hasNewerTrack(targetId)) setupMarquees();
}

async function runHide() {
  if (currentId == null) {
    stage.hidden = true;
    return;
  }

  clearAnimClasses();
  void card.offsetWidth;
  card.classList.add("is-exit");
  await sleep(EXIT_MS);
  clearAnimClasses();
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
  jacketImg.removeAttribute("src");
  jacketImg.removeAttribute("data-track-id");
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
        await runHide();
        continue;
      }

      if (currentId === track.id) {
        stage.hidden = false;
        applyProgressOnly(state);
        continue;
      }

      await runExitEnter(state);
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
  if (
    currentId != null &&
    !card.classList.contains("is-enter") &&
    !card.classList.contains("is-exit")
  ) {
    setupMarquees();
  }
});

syncUiScale();
poll();
setInterval(poll, 250);
