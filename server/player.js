import { publicTrack } from "./library.js";

function shuffleInPlace(arr) {
  for (let i = arr.length - 1; i > 0; i -= 1) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}

export class Player {
  constructor() {
    this.library = null;
    this.queue = [];
    this.index = -1;
    this.playing = false;
    this.shuffle = true;
    this.position = 0;
    this.duration = 0;
  }

  setLibrary(library) {
    this.library = library;
    this.rebuildQueue({ keepCurrent: false });
  }

  currentTrack() {
    if (this.index < 0 || this.index >= this.queue.length) return null;
    return this.queue[this.index] ?? null;
  }

  rebuildQueue({ keepCurrent }) {
    const tracks = this.library?.tracks ?? [];
    const currentId = keepCurrent ? this.currentTrack()?.id : null;

    if (tracks.length === 0) {
      this.queue = [];
      this.index = -1;
      this.playing = false;
      this.position = 0;
      this.duration = 0;
      return;
    }

    const next = [...tracks];
    if (this.shuffle) shuffleInPlace(next);
    this.queue = next;

    if (currentId) {
      const found = this.queue.findIndex((t) => t.id === currentId);
      this.index = found >= 0 ? found : 0;
    } else {
      this.index = 0;
    }
  }

  play() {
    if (!this.currentTrack()) {
      if (this.queue.length === 0) this.rebuildQueue({ keepCurrent: false });
      if (this.queue.length === 0) return;
      this.index = Math.max(0, this.index);
    }
    this.playing = true;
  }

  pause() {
    this.playing = false;
  }

  toggle() {
    if (this.playing) this.pause();
    else this.play();
  }

  next() {
    if (this.queue.length === 0) return;
    if (this.index >= this.queue.length - 1) {
      this.rebuildQueue({ keepCurrent: false });
      this.index = 0;
    } else {
      this.index += 1;
    }
    this.position = 0;
    this.duration = 0;
    this.playing = true;
  }

  prev() {
    if (this.queue.length === 0) return;
    if (this.position > 3) {
      this.position = 0;
      this.playing = true;
      return;
    }
    if (this.index <= 0) {
      this.index = this.queue.length - 1;
    } else {
      this.index -= 1;
    }
    this.position = 0;
    this.duration = 0;
    this.playing = true;
  }

  setShuffle(enabled) {
    this.shuffle = Boolean(enabled);
    this.rebuildQueue({ keepCurrent: true });
  }

  setProgress({ position, duration, ended }) {
    if (typeof position === "number" && Number.isFinite(position)) {
      this.position = Math.max(0, position);
    }
    if (typeof duration === "number" && Number.isFinite(duration)) {
      this.duration = Math.max(0, duration);
    }
    if (ended) {
      this.next();
      return true;
    }
    return false;
  }

  snapshot() {
    const track = this.currentTrack();
    return {
      playing: this.playing,
      shuffle: this.shuffle,
      position: this.position,
      duration: this.duration,
      index: this.index,
      queueLength: this.queue.length,
      track: publicTrack(track),
      library: this.library
        ? {
            root: this.library.root,
            albumCount: this.library.albums.length,
            singleCount: this.library.singles.length,
            trackCount: this.library.tracks.length,
            scannedAt: this.library.scannedAt,
            albums: this.library.albums.map((a) => ({
              id: a.id,
              name: a.name,
              trackCount: a.trackCount,
              coverUrl: a.coverPath
                ? `/cover/${a.tracks[0]?.id ?? a.id}`
                : null,
            })),
            singles: this.library.singles.map(publicTrack),
          }
        : null,
    };
  }
}
