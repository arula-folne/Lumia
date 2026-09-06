import fs from "node:fs";
import path from "node:path";

const AUDIO_EXT = new Set([
  ".mp3",
  ".flac",
  ".m4a",
  ".aac",
  ".wav",
  ".ogg",
  ".opus",
]);

const COVER_NAMES = new Set(["cover.png", "cover.jpg", "cover.jpeg", "cover.webp"]);

function isAudioFile(name) {
  return AUDIO_EXT.has(path.extname(name).toLowerCase());
}

function findCover(dirPath, entries) {
  const lowerMap = new Map(entries.map((e) => [e.toLowerCase(), e]));
  for (const candidate of COVER_NAMES) {
    const real = lowerMap.get(candidate);
    if (real) return path.join(dirPath, real);
  }
  return null;
}

function trackId(filePath) {
  return Buffer.from(filePath).toString("base64url");
}

function titleFromFile(fileName) {
  return path.parse(fileName).name;
}

/**
 * Scan music root.
 * - Subfolder (not "music"): album (folder name + cover.png + audio)
 * - Subfolder "music": singles
 * - Audio files in root: singles
 */
export function scanLibrary(rootDir) {
  const resolved = path.resolve(rootDir);
  if (!fs.existsSync(resolved) || !fs.statSync(resolved).isDirectory()) {
    throw new Error(`フォルダが見つかりません: ${resolved}`);
  }

  const albums = [];
  const singles = [];
  const tracks = [];

  const topEntries = fs.readdirSync(resolved, { withFileTypes: true });

  for (const entry of topEntries) {
    const full = path.join(resolved, entry.name);

    if (entry.isFile() && isAudioFile(entry.name)) {
      const track = {
        id: trackId(full),
        title: titleFromFile(entry.name),
        album: null,
        albumId: null,
        filePath: full,
        coverPath: null,
        kind: "single",
      };
      singles.push(track);
      tracks.push(track);
      continue;
    }

    if (!entry.isDirectory()) continue;

    const isMusicFolder = entry.name.toLowerCase() === "music";
    const children = fs.readdirSync(full);
    const coverPath = isMusicFolder ? null : findCover(full, children);
    const audioFiles = children.filter(isAudioFile).sort((a, b) =>
      a.localeCompare(b, "ja"),
    );

    if (isMusicFolder) {
      for (const file of audioFiles) {
        const filePath = path.join(full, file);
        const track = {
          id: trackId(filePath),
          title: titleFromFile(file),
          album: null,
          albumId: null,
          filePath,
          coverPath: null,
          kind: "single",
        };
        singles.push(track);
        tracks.push(track);
      }
      continue;
    }

    const albumId = trackId(full);
    const albumTracks = [];
    for (const file of audioFiles) {
      const filePath = path.join(full, file);
      const track = {
        id: trackId(filePath),
        title: titleFromFile(file),
        album: entry.name,
        albumId,
        filePath,
        coverPath,
        kind: "album",
      };
      albumTracks.push(track);
      tracks.push(track);
    }

    albums.push({
      id: albumId,
      name: entry.name,
      coverPath,
      trackCount: albumTracks.length,
      tracks: albumTracks,
    });
  }

  albums.sort((a, b) => a.name.localeCompare(b.name, "ja"));

  return {
    root: resolved,
    albums,
    singles,
    tracks,
    scannedAt: Date.now(),
  };
}

export function publicTrack(track) {
  if (!track) return null;
  return {
    id: track.id,
    title: track.title,
    album: track.album,
    albumId: track.albumId,
    kind: track.kind,
    audioUrl: `/media/${track.id}`,
    coverUrl: track.coverPath ? `/cover/${track.id}` : null,
  };
}
