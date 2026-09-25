/* AETHERKIRI_RANGE_FS */
(function () {
var userFsSyncTimer = 0;
var userFsSyncRunning = false;
var userFsSyncQueued = false;

function isUserFsPath(path) {
return String(path || "").replace(/\\/g, "/").indexOf("/userfs/") === 0 || String(path || "") === "/userfs";
}

function streamPath(stream) {
try {
return stream && stream.node && typeof FS.getPath === "function" ? FS.getPath(stream.node) : "";
} catch (e) {
return "";
}
}

function flushUserFs(reason) {
if (typeof FS.syncfs !== "function") return;
if (userFsSyncRunning) {
userFsSyncQueued = true;
return;
}
userFsSyncRunning = true;
try {
FS.syncfs(false, function (err) {
userFsSyncRunning = false;
if (err) console.warn("AetherKiri userfs sync failed:", err);
if (userFsSyncQueued) {
userFsSyncQueued = false;
scheduleUserFsSync("queued");
}
});
} catch (e) {
userFsSyncRunning = false;
console.warn("AetherKiri userfs sync failed:", e);
}
}

function scheduleUserFsSync(reason) {
if (userFsSyncTimer) clearTimeout(userFsSyncTimer);
userFsSyncTimer = setTimeout(function () {
userFsSyncTimer = 0;
flushUserFs(reason || "change");
}, 750);
}

function installUserFsSyncHooks() {
if (!globalThis.FS || FS.__aetherKiriUserFsSyncHooksInstalled) return;
FS.__aetherKiriUserFsSyncHooksInstalled = true;
function wrapPathMutation(name, pathIndex) {
var original = FS[name];
if (typeof original !== "function") return;
FS[name] = function () {
var result = original.apply(this, arguments);
try {
var path = arguments[pathIndex];
if (isUserFsPath(path)) scheduleUserFsSync(name);
} catch (e) {}
return result;
};
}
function wrapRename() {
var original = FS.rename;
if (typeof original !== "function") return;
FS.rename = function (oldPath, newPath) {
var result = original.apply(this, arguments);
if (isUserFsPath(oldPath) || isUserFsPath(newPath)) scheduleUserFsSync("rename");
return result;
};
}
function wrapStreamMutation(name) {
var original = FS[name];
if (typeof original !== "function") return;
FS[name] = function () {
var path = streamPath(arguments[0]);
var result = original.apply(this, arguments);
if (isUserFsPath(path)) scheduleUserFsSync(name);
return result;
};
}
wrapPathMutation("writeFile", 0);
wrapPathMutation("unlink", 0);
wrapPathMutation("rmdir", 0);
wrapPathMutation("mkdir", 0);
wrapPathMutation("mkdirTree", 0);
wrapPathMutation("truncate", 0);
wrapPathMutation("chmod", 0);
wrapPathMutation("utime", 0);
wrapRename();
wrapStreamMutation("write");
wrapStreamMutation("close");
}

installUserFsSyncHooks();

globalThis.AetherKiriSyncUserFs = function (reason) {
if (userFsSyncTimer) {
clearTimeout(userFsSyncTimer);
userFsSyncTimer = 0;
}
flushUserFs(reason || "manual");
return JSON.stringify({ ok: true, running: userFsSyncRunning });
};

if (typeof window === "object" && !window.__aetherKiriUserFsLifecycleSyncInstalled) {
window.__aetherKiriUserFsLifecycleSyncInstalled = true;
window.addEventListener("pagehide", function () { flushUserFs("pagehide"); });
window.addEventListener("beforeunload", function () { flushUserFs("beforeunload"); });
if (typeof document === "object") {
document.addEventListener("visibilitychange", function () {
if (document.visibilityState === "hidden") flushUserFs("hidden");
});
}
}

var AetherKiriRangeFS = {
DIR_MODE: 16895,
FILE_MODE: 33279,
mounted: {},
mount: function (mount) {
var opts = mount.opts || {};
var root = AetherKiriRangeFS.createNode(null, "/", AetherKiriRangeFS.DIR_MODE, 0, null);
var createdParents = { "": root };
function ensureParent(path) {
var parts = path.split("/").filter(Boolean);
var parent = root;
for (var i = 0; i < parts.length - 1; i++) {
var key = parts.slice(0, i + 1).join("/");
if (!createdParents[key]) {
createdParents[key] = AetherKiriRangeFS.createNode(parent, parts[i], AetherKiriRangeFS.DIR_MODE, 0, null);
}
parent = createdParents[key];
}
return parent;
}
function base(path) {
var parts = path.split("/");
return parts[parts.length - 1];
}
var files = (opts.files || []).slice();
function hasManifestPath(path) {
var target = String(path || "").replace(/\\/g, "/").split("/").filter(Boolean).join("/").toLowerCase();
return files.some(function (file) {
return String(file && file.path || "").replace(/\\/g, "/").split("/").filter(Boolean).join("/").toLowerCase() === target;
});
}
function addRuntimeFont(path, fontPath) {
try {
if (hasManifestPath(path) || !FS.analyzePath(fontPath).exists) return;
var stat = FS.stat(fontPath);
files.push({
path: path,
size: stat.size || 0,
mtimeMs: Date.now(),
memfsPath: fontPath
});
} catch (e) {}
}
addRuntimeFont("default.ttf", "/fonts/default.ttf");
addRuntimeFont("fonts/default.ttf", "/fonts/default.ttf");
addRuntimeFont("symbols.ttf", "/fonts/symbols.ttf");
addRuntimeFont("fonts/symbols.ttf", "/fonts/symbols.ttf");
for (var file of files) {
if (!file || !file.path) continue;
AetherKiriRangeFS.createNode(ensureParent(file.path), base(file.path), AetherKiriRangeFS.FILE_MODE, 0, {
baseUrl: opts.baseUrl || "",
blobUrl: file.blobUrl || "",
fileId: file.fileId || "",
memfsPath: file.memfsPath || "",
memfsData: null,
path: file.path,
size: file.size || 0,
mtimeMs: file.mtimeMs || Date.now()
});
}
return root;
},
createNode: function (parent, name, mode, dev, contents) {
var node = FS.createNode(parent, name, mode);
node.mode = mode;
node.node_ops = AetherKiriRangeFS.node_ops;
node.stream_ops = AetherKiriRangeFS.stream_ops;
node.atime = node.mtime = node.ctime = contents && contents.mtimeMs || Date.now();
if (mode === AetherKiriRangeFS.FILE_MODE) {
node.size = contents.size;
node.contents = contents;
} else {
node.size = 4096;
node.contents = {};
}
if (parent) {
parent.contents[name] = node;
}
return node;
},
node_ops: {
getattr: function (node) {
return { dev: 1, ino: node.id, mode: node.mode, nlink: 1, uid: 0, gid: 0, rdev: 0, size: node.size, atime: new Date(node.atime), mtime: new Date(node.mtime), ctime: new Date(node.ctime), blksize: 4096, blocks: Math.ceil(node.size / 4096) };
},
setattr: function (node, attr) {
for (var key of ["mode", "atime", "mtime", "ctime"]) {
if (attr[key] != null) node[key] = attr[key];
}
},
lookup: function (parent, name) {
var node = parent.contents && parent.contents[name];
if (!node) throw new FS.ErrnoError(ERRNO_CODES.ENOENT);
return node;
},
mknod: function () { throw new FS.ErrnoError(ERRNO_CODES.EPERM); },
rename: function () { throw new FS.ErrnoError(ERRNO_CODES.EPERM); },
unlink: function () { throw new FS.ErrnoError(ERRNO_CODES.EPERM); },
rmdir: function () { throw new FS.ErrnoError(ERRNO_CODES.EPERM); },
readdir: function (node) {
var entries = [".", ".."];
for (var key of Object.keys(node.contents)) entries.push(key);
return entries;
},
symlink: function () { throw new FS.ErrnoError(ERRNO_CODES.EPERM); }
},
stream_ops: {
read: function (stream, buffer, offset, length, position) {
if (position == null) position = stream.position;
if (position >= stream.node.size) return 0;
var size = Math.min(length, stream.node.size - position);
var file = stream.node.contents;
if (file.memfsPath) {
var data = file.memfsData;
if (!data) {
data = FS.readFile(file.memfsPath);
file.memfsData = data;
}
var countFromMemfs = Math.min(size, data.length - position);
if (countFromMemfs <= 0) return 0;
for (var k = 0; k < countFromMemfs; k++) buffer[offset + k] = data[position + k];
return countFromMemfs;
}
if (file.fileId && typeof globalThis.AetherKiriReadLocalFileSync === "function" && typeof globalThis.AetherKiriCanReadLocalFileSync === "function" && globalThis.AetherKiriCanReadLocalFileSync()) {
try {
var bytes = globalThis.AetherKiriReadLocalFileSync(file.fileId, position, size);
var countFromFile = Math.min(bytes.length, size);
for (var j = 0; j < countFromFile; j++) buffer[offset + j] = bytes[j];
return countFromFile;
} catch (e) {
console.warn("AetherKiri local file direct read failed, falling back to blob range read:", e);
}
}
var count = 0;
var maxChunkSize = 16 * 1024 * 1024;
while (count < size) {
var chunkPosition = position + count;
var chunkSize = Math.min(size - count, maxChunkSize);
var xhr = new XMLHttpRequest();
if (file.blobUrl) {
xhr.open("GET", file.blobUrl, false);
xhr.setRequestHeader("Range", "bytes=" + chunkPosition + "-" + (chunkPosition + chunkSize - 1));
} else {
var url = file.baseUrl + "/file?path=" + encodeURIComponent(file.path) + "&offset=" + chunkPosition + "&length=" + chunkSize;
xhr.open("GET", url, false);
}
xhr.overrideMimeType("text/plain; charset=x-user-defined");
xhr.send(null);
if (xhr.status !== 200 && xhr.status !== 206) throw new FS.ErrnoError(ERRNO_CODES.EIO);
if (file.blobUrl && xhr.status === 200 && stream.node.size > chunkSize) throw new FS.ErrnoError(ERRNO_CODES.EIO);
var text = xhr.responseText || "";
var chunkCount = Math.min(text.length, chunkSize);
if (chunkCount <= 0) throw new FS.ErrnoError(ERRNO_CODES.EIO);
for (var i = 0; i < chunkCount; i++) buffer[offset + count + i] = text.charCodeAt(i) & 255;
count += chunkCount;
}
return count;
},
write: function () { throw new FS.ErrnoError(ERRNO_CODES.EIO); },
llseek: function (stream, offset, whence) {
var position = offset;
if (whence === 1) position += stream.position;
else if (whence === 2 && FS.isFile(stream.node.mode)) position += stream.node.size;
if (position < 0) throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
return position;
}
}
};

Module["aetherKiriMountRangeGame"] = function (mountPoint, opts, manifestJson) {
var manifest = typeof manifestJson === "string" ? JSON.parse(manifestJson) : manifestJson;
if (!FS.analyzePath("/webgames").exists) FS.mkdirTree("/webgames");
if (FS.analyzePath(mountPoint).exists) {
try { FS.unmount(mountPoint); } catch (e) {}
} else {
FS.mkdirTree(mountPoint);
}
FS.mount(AetherKiriRangeFS, { baseUrl: opts && opts.baseUrl || "", files: manifest && manifest.files || [] }, mountPoint);
AetherKiriRangeFS.mounted[mountPoint] = true;
seedSavedata(mountPoint, manifest);
return true;
};
Module["aetherKiriMountHttpGame"] = function (mountPoint, baseUrl, manifestJson) {
return Module["aetherKiriMountRangeGame"](mountPoint, { baseUrl: baseUrl }, manifestJson);
};
Module["aetherKiriIsRangeGameMounted"] = function (mountPoint) {
return !!AetherKiriRangeFS.mounted[mountPoint];
};
globalThis.AetherKiriMountHttpGame = function (mountPoint, baseUrl, manifestJson) {
try {
Module["aetherKiriMountHttpGame"](mountPoint, baseUrl, manifestJson);
return JSON.stringify({ ok: true });
} catch (e) {
return JSON.stringify({ ok: false, error: String(e && e.message ? e.message : e) });
}
};
globalThis.AetherKiriMountLocalBlobGame = function (mountPoint, gameId) {
try {
if (typeof globalThis.AetherKiriLocalGameManifest !== "function") throw new Error("Local game manifest store is not ready");
var manifestJson = globalThis.AetherKiriLocalGameManifest(gameId);
Module["aetherKiriMountRangeGame"](mountPoint, {}, manifestJson);
return JSON.stringify({ ok: true });
} catch (e) {
return JSON.stringify({ ok: false, error: String(e && e.message ? e.message : e) });
}
};
globalThis.AetherKiriIsHttpGameMounted = function (mountPoint) {
try { return !!Module["aetherKiriIsRangeGameMounted"](mountPoint); } catch (e) { return false; }
};

function savedataRootForMount(mountPoint) {
var parts = String(mountPoint || "").split("/").filter(Boolean);
var key = parts.length > 0 ? parts[parts.length - 1] : "default";
key = key.replace(/[^A-Za-z0-9_-]/g, "_");
if (!key) key = "default";
return "/userfs/aetherkiri/savedata/" + key;
}

function ensureDirectory(path) {
if (!path || FS.analyzePath(path).exists) return;
FS.mkdirTree(path);
}

function seedSavedata(mountPoint, manifest) {
try {
var files = manifest && manifest.files || [];
if (!Array.isArray(files) || files.length === 0) return;
var targetRoot = savedataRootForMount(mountPoint);
ensureDirectory(targetRoot);
var copied = 0;
var skipped = 0;
for (var i = 0; i < files.length; i++) {
var rel = String(files[i] && files[i].path || "").replace(/\\/g, "/");
if (!/^savedata\//i.test(rel)) continue;
var sub = rel.slice("savedata/".length);
if (!sub || /(^|\/)\.\.?($|\/)/.test(sub)) {
skipped++;
continue;
}
var dest = targetRoot + "/" + sub;
if (FS.analyzePath(dest).exists) {
skipped++;
continue;
}
var slash = dest.lastIndexOf("/");
if (slash > 0) ensureDirectory(dest.slice(0, slash));
var data = FS.readFile(mountPoint + "/" + rel);
FS.writeFile(dest, data);
copied++;
}
if (copied > 0) {
console.log("AetherKiri seeded savedata files:", copied, "skipped:", skipped, "target:", targetRoot);
scheduleUserFsSync("seed savedata");
}
} catch (e) {
console.warn("AetherKiri savedata seed failed:", e);
}
}
})();
/* AETHERKIRI_RANGE_FS_END */
