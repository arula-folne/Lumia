const stage = document.getElementById("stage");
const card = document.getElementById("card");
const jacketImg = document.getElementById("jacket-img");
const jacketFallback = document.getElementById("jacket-fallback");
const albumEl = document.getElementById("album");
const titleEl = document.getElementById("title");
const artistEl = document.getElementById("artist");
const durationEl = document.getElementById("duration");

const EXIT_MS = 420;
const JACKET_MS = 420;
const TEXT_START_MS = 300;
const CHAR_STAGGER_MS = 32;
const CHAR_DUR_MS = 280;

let currentId = null;
let busy = false;
let pendingState = null;

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

function fillChars(el, text, startIndex) {
  el.replaceChildren();
  const chars = Array.from(text);
  chars.forEach((ch, i) => {
    const span = document.createElement("span");
    span.className = "lumia-char";
    span.style.setProperty("--i", String(startIndex + i));
    span.textContent = ch === " " ? "\u00A0" : ch;
    el.appendChild(span);
  });
  return startIndex + chars.length;
}

function setOptionalChars(el, value, startIndex) {
  const text = typeof value === "string" ? value.trim() : "";
  if (!text) {
    el.hidden = true;
    el.replaceChildren();
    return startIndex;
  }
  el.hidden = false;
  return fillChars(el, text, startIndex);
}

function clearAnimClasses() {
  card.classList.remove("is-exit", "is-enter");
}

function restartEnterAnim() {
  clearAnimClasses();
  void card.offsetWidth;
  card.classList.add("is-enter");
}

/** @returns {number} total animated character count */
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

async function runExitEnter(nextState) {
  const hadTrack = currentId != null;

  if (hadTrack) {
    clearAnimClasses();
    void card.offsetWidth;
    card.classList.add("is-exit");
    await sleep(EXIT_MS);
  }

  stage.hidden = false;
  const charCount = applyContent(nextState);
  currentId = nextState.track.id;
  restartEnterAnim();
  await sleep(enterDurationMs(charCount));
  card.classList.remove("is-enter");
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

poll();
setInterval(poll, 250);
