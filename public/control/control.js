const rootInput = document.getElementById("root-input");
const libStatus = document.getElementById("lib-status");
const playStatus = document.getElementById("play-status");
const libraryList = document.getElementById("library-list");
const overlayUrl = document.getElementById("overlay-url");
const nowTitle = document.getElementById("now-title");
const nowAlbum = document.getElementById("now-album");
const nowTime = document.getElementById("now-time");
const nowCover = document.getElementById("now-cover");
const chkShuffle = document.getElementById("chk-shuffle");
const btnToggle = document.getElementById("btn-toggle");

const STORAGE_KEY = "lumia.root";

overlayUrl.textContent = `${location.origin}/overlay/`;

const saved = localStorage.getItem(STORAGE_KEY);
if (saved) rootInput.value = saved;

function fmt(sec) {
  const s = Math.max(0, Math.floor(sec || 0));
  const m = Math.floor(s / 60);
  const r = String(s % 60).padStart(2, "0");
  return `${m}:${r}`;
}

async function api(path, body) {
  const res = await fetch(path, {
    method: body ? "POST" : "GET",
    headers: body ? { "Content-Type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

function renderLibrary(state) {
  const lib = state.library;
  if (!lib) {
    libraryList.innerHTML = "";
    libStatus.textContent = "フォルダ未設定";
    return;
  }

  libStatus.textContent = `${lib.root} — アルバム ${lib.albumCount} / 単曲 ${lib.singleCount} / 合計 ${lib.trackCount} 曲`;

  const parts = [];
  for (const album of lib.albums) {
    parts.push(
      `<article class="card"><h3>${escapeHtml(album.name)}</h3><p>${album.trackCount} 曲</p></article>`,
    );
  }
  if (lib.singleCount > 0) {
    parts.push(
      `<article class="card"><h3>単曲</h3><p>${lib.singleCount} 曲</p></article>`,
    );
  }
  libraryList.innerHTML = parts.join("") || `<p class="status">曲が見つかりませんでした</p>`;
}

function renderNow(state) {
  const track = state.track;
  nowTitle.textContent = track?.title || "—";
  nowAlbum.textContent = track?.album || (track ? "単曲" : "—");
  nowTime.textContent = `${fmt(state.position)} / ${fmt(state.duration)}`;
  btnToggle.textContent = state.playing ? "⏸" : "▶";
  chkShuffle.checked = state.shuffle;
  playStatus.textContent = state.queueLength
    ? `キュー ${state.index + 1} / ${state.queueLength}`
    : "";

  if (track?.coverUrl) {
    nowCover.style.backgroundImage = `url("${track.coverUrl}")`;
  } else {
    nowCover.style.backgroundImage = "";
  }
}

function escapeHtml(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function applyState(state) {
  renderLibrary(state);
  renderNow(state);
}

async function loadLibrary() {
  const root = rootInput.value.trim();
  if (!root) {
    libStatus.textContent = "パスを入力してください";
    libStatus.classList.add("error");
    return;
  }
  try {
    libStatus.classList.remove("error");
    const state = await api("/api/library", { root });
    localStorage.setItem(STORAGE_KEY, root);
    applyState(state);
  } catch (err) {
    libStatus.textContent = err.message;
    libStatus.classList.add("error");
  }
}

document.getElementById("btn-load").addEventListener("click", loadLibrary);
document.getElementById("btn-rescan").addEventListener("click", async () => {
  try {
    libStatus.classList.remove("error");
    applyState(await api("/api/rescan", {}));
  } catch (err) {
    libStatus.textContent = err.message;
    libStatus.classList.add("error");
  }
});

async function control(action, extra = {}) {
  try {
    applyState(await api("/api/control", { action, ...extra }));
  } catch (err) {
    playStatus.textContent = err.message;
  }
}

document.getElementById("btn-prev").addEventListener("click", () => control("prev"));
document.getElementById("btn-next").addEventListener("click", () => control("next"));
document.getElementById("btn-toggle").addEventListener("click", () => control("toggle"));
chkShuffle.addEventListener("change", () =>
  control("shuffle", { enabled: chkShuffle.checked }),
);

function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.addEventListener("message", (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      if (msg.type === "state" && msg.state) applyState(msg.state);
    } catch {
      /* ignore */
    }
  });
  ws.addEventListener("close", () => setTimeout(connectWs, 1000));
}

connectWs();
api("/api/state").then(applyState).catch(() => {});
