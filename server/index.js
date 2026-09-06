import express from "express";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer } from "ws";
import { scanLibrary } from "./library.js";
import { Player } from "./player.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const PUBLIC = path.join(ROOT, "public");
const PORT = Number(process.env.LUMIA_PORT || 8787);

const app = express();
const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: "/ws" });
const player = new Player();
let lastProgressBroadcast = 0;

const MIME = {
  ".mp3": "audio/mpeg",
  ".flac": "audio/flac",
  ".m4a": "audio/mp4",
  ".aac": "audio/aac",
  ".wav": "audio/wav",
  ".ogg": "audio/ogg",
  ".opus": "audio/ogg",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".webp": "image/webp",
};

app.use(express.json({ limit: "1mb" }));
app.use(express.static(PUBLIC));

function findTrackById(id) {
  return player.library?.tracks.find((t) => t.id === id) ?? null;
}

function broadcast(extra = {}) {
  const payload = JSON.stringify({ type: "state", state: player.snapshot(), ...extra });
  for (const client of wss.clients) {
    if (client.readyState === 1) client.send(payload);
  }
}

function broadcastProgress() {
  const now = Date.now();
  if (now - lastProgressBroadcast < 400) return;
  lastProgressBroadcast = now;
  broadcast({ reason: "progress" });
}

function sendFile(res, filePath) {
  if (!filePath || !fs.existsSync(filePath)) {
    res.status(404).end("Not found");
    return;
  }
  const ext = path.extname(filePath).toLowerCase();
  res.setHeader("Content-Type", MIME[ext] || "application/octet-stream");
  res.setHeader("Cache-Control", "no-cache");
  fs.createReadStream(filePath).pipe(res);
}

app.get("/", (_req, res) => {
  res.redirect("/control/");
});

app.get("/api/state", (_req, res) => {
  res.json(player.snapshot());
});

app.post("/api/library", (req, res) => {
  try {
    const root = String(req.body?.root || "").trim();
    if (!root) {
      res.status(400).json({ error: "root が必要です" });
      return;
    }
    const library = scanLibrary(root);
    player.setLibrary(library);
    broadcast();
    res.json(player.snapshot());
  } catch (err) {
    res.status(400).json({ error: err.message || String(err) });
  }
});

app.post("/api/rescan", (_req, res) => {
  try {
    if (!player.library?.root) {
      res.status(400).json({ error: "先にフォルダを指定してください" });
      return;
    }
    const library = scanLibrary(player.library.root);
    player.setLibrary(library);
    broadcast();
    res.json(player.snapshot());
  } catch (err) {
    res.status(400).json({ error: err.message || String(err) });
  }
});

app.post("/api/control", (req, res) => {
  const action = String(req.body?.action || "");
  switch (action) {
    case "play":
      player.play();
      break;
    case "pause":
      player.pause();
      break;
    case "toggle":
      player.toggle();
      break;
    case "next":
      player.next();
      break;
    case "prev":
      player.prev();
      break;
    case "shuffle":
      player.setShuffle(req.body?.enabled ?? !player.shuffle);
      break;
    case "progress": {
      const changed = player.setProgress({
        position: req.body?.position,
        duration: req.body?.duration,
        ended: Boolean(req.body?.ended),
      });
      if (changed) broadcast({ reason: "track-ended" });
      else broadcastProgress();
      res.json(player.snapshot());
      return;
    }
    default:
      res.status(400).json({ error: `不明な action: ${action}` });
      return;
  }
  broadcast();
  res.json(player.snapshot());
});

app.get("/media/:id", (req, res) => {
  const track = findTrackById(req.params.id);
  if (!track) {
    res.status(404).end("Not found");
    return;
  }
  sendFile(res, track.filePath);
});

app.get("/cover/:id", (req, res) => {
  const track = findTrackById(req.params.id);
  if (!track?.coverPath) {
    // album cover lookup by album folder id fallback unused; 404 -> overlay placeholder
    res.status(404).end("Not found");
    return;
  }
  sendFile(res, track.coverPath);
});

wss.on("connection", (ws) => {
  ws.send(JSON.stringify({ type: "state", state: player.snapshot() }));
  ws.on("message", (raw) => {
    let msg;
    try {
      msg = JSON.parse(String(raw));
    } catch {
      return;
    }
    if (msg.type === "progress") {
      const changed = player.setProgress({
        position: msg.position,
        duration: msg.duration,
        ended: Boolean(msg.ended),
      });
      if (changed) broadcast({ reason: "track-ended" });
      else broadcastProgress();
    } else if (msg.type === "ping") {
      ws.send(JSON.stringify({ type: "pong" }));
    }
  });
});

server.listen(PORT, "127.0.0.1", () => {
  console.log("");
  console.log("  Lumia — Live x Music x View");
  console.log(`  Control : http://127.0.0.1:${PORT}/control/`);
  console.log(`  Overlay : http://127.0.0.1:${PORT}/overlay/`);
  console.log("");
});
