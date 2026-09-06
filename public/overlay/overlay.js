const stage = document.getElementById("stage");
const card = document.getElementById("card");
const jacketImg = document.getElementById("jacket-img");
const jacketFallback = document.getElementById("jacket-fallback");
const albumEl = document.getElementById("album");
const titleEl = document.getElementById("title");
const artistEl = document.getElementById("artist");
const durationEl = document.getElementById("duration");

let currentId = null;

function fmt(sec) {
  const s = Math.max(0, Math.floor(sec || 0));
  const m = Math.floor(s / 60);
  const r = String(s % 60).padStart(2, "0");
  return `${m}:${r}`;
}

function setOptionalText(el, value) {
  const text = typeof value === "string" ? value.trim() : "";
  if (text) {
    el.hidden = false;
    el.textContent = text;
  } else {
    el.hidden = true;
    el.textContent = "";
  }
}

function playEnterAnim() {
  card.classList.remove("is-enter");
  void card.offsetWidth;
  card.classList.add("is-enter");
}

function applyState(state) {
  const track = state.track;
  if (!track) {
    stage.hidden = true;
    currentId = null;
    durationEl.textContent = "0:00 / 0:00";
    setOptionalText(albumEl, "");
    setOptionalText(artistEl, "");
    return;
  }

  stage.hidden = false;
  setOptionalText(albumEl, track.album);
  titleEl.textContent = track.title || "";
  setOptionalText(artistEl, track.artist);
  durationEl.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;

  if (track.coverUrl) {
    jacketImg.hidden = false;
    jacketFallback.hidden = true;
    if (jacketImg.getAttribute("src") !== track.coverUrl) jacketImg.src = track.coverUrl;
  } else {
    jacketImg.hidden = true;
    jacketImg.removeAttribute("src");
    jacketFallback.hidden = false;
  }

  if (currentId !== track.id) {
    currentId = track.id;
    playEnterAnim();
  }
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
