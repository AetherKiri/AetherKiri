/* AETHERKIRI_LOCAL_PICKER */
(function () {
if (window.AetherKiriPickLocalGame) return;

var store = window.AetherKiriLocalGameStore || {
picks: {},
games: {},
files: {},
readerWorker: null,
registeredFiles: {},
persistedGames: {}
};
window.AetherKiriLocalGameStore = store;
var atomicsWaitSupported = null;
var localGameDbPromise = null;
var localGameRestorePromise = null;
var localGameRestoreDone = false;
var localGameRestoreError = "";
var localGameDbName = "AetherKiriLocalGames";
var localGameDbStore = "games";

function nextId() {
if (window.crypto && typeof window.crypto.randomUUID === "function") return "local-" + window.crypto.randomUUID().replace(/-/g, "");
return "local-" + Date.now().toString(36) + Math.random().toString(36).slice(2);
}

function stableHash(text) {
var h1 = 0xdeadbeef ^ text.length;
var h2 = 0x41c6ce57 ^ text.length;
for (var i = 0; i < text.length; i++) {
var ch = text.charCodeAt(i);
h1 = Math.imul(h1 ^ ch, 2654435761);
h2 = Math.imul(h2 ^ ch, 1597334677);
}
h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
return ((h2 >>> 0).toString(16).padStart(8, "0") + (h1 >>> 0).toString(16).padStart(8, "0"));
}

function stableGameId(name, type, files) {
var parts = [String(type || ""), "\n", cleanPath(name).toLowerCase(), "\n"];
for (var i = 0; i < files.length; i++) {
var file = files[i];
parts.push(String(file.path || "").toLowerCase(), "\t", String(file.size || 0), "\n");
}
return "local-" + stableHash(parts.join(""));
}

function canPersistHandles() {
return typeof indexedDB === "object" && (typeof window.showDirectoryPicker === "function" || typeof window.showOpenFilePicker === "function");
}

function openLocalGameDb() {
if (!canPersistHandles()) return Promise.reject(new Error("Persistent local game handles are unavailable"));
if (localGameDbPromise) return localGameDbPromise;
localGameDbPromise = new Promise(function (resolve, reject) {
var request = indexedDB.open(localGameDbName, 1);
request.onupgradeneeded = function () {
var db = request.result;
if (!db.objectStoreNames.contains(localGameDbStore)) {
db.createObjectStore(localGameDbStore, { keyPath: "id" });
}
};
request.onsuccess = function () { resolve(request.result); };
request.onerror = function () { reject(request.error || new Error("IndexedDB open failed")); };
});
return localGameDbPromise;
}

function readPersistedRecords() {
if (!canPersistHandles()) return Promise.resolve([]);
return openLocalGameDb().then(function (db) {
return new Promise(function (resolve, reject) {
var tx = db.transaction(localGameDbStore, "readonly");
var request = tx.objectStore(localGameDbStore).getAll();
request.onsuccess = function () { resolve(request.result || []); };
request.onerror = function () { reject(request.error || new Error("IndexedDB read failed")); };
});
}).catch(function (error) {
localGameRestoreError = String(error && error.message ? error.message : error);
return [];
});
}

function writePersistedRecord(record) {
if (!canPersistHandles() || !record || !record.id) return Promise.resolve(false);
return openLocalGameDb().then(function (db) {
return new Promise(function (resolve, reject) {
var tx = db.transaction(localGameDbStore, "readwrite");
var request = tx.objectStore(localGameDbStore).put(record);
request.onsuccess = function () { resolve(true); };
request.onerror = function () { reject(request.error || new Error("IndexedDB write failed")); };
});
}).catch(function (error) {
console.warn("AetherKiri local game handle persistence failed:", error);
return false;
});
}

async function queryReadPermission(handle) {
if (!handle || typeof handle.queryPermission !== "function") return "granted";
try {
return await handle.queryPermission({ mode: "read" });
} catch (e) {
return "prompt";
}
}

async function requestReadPermission(handle) {
var permission = await queryReadPermission(handle);
if (permission === "granted") return true;
if (!handle || typeof handle.requestPermission !== "function") return false;
try {
permission = await handle.requestPermission({ mode: "read" });
return permission === "granted";
} catch (e) {
return false;
}
}

async function findPersistedRecordForHandle(kind, handle) {
if (!handle || typeof handle.isSameEntry !== "function") return null;
var records = await readPersistedRecords();
for (var i = 0; i < records.length; i++) {
var record = records[i];
var saved = kind === "directory" ? record.directoryHandle : record.fileHandle;
if (!saved || typeof saved.isSameEntry !== "function") continue;
try {
if (await saved.isSameEntry(handle)) return record;
} catch (e) {}
}
return null;
}

function ensureReaderWorker() {
if (store.readerWorker) return store.readerWorker;
if (typeof window.SharedArrayBuffer !== "function" || typeof window.Atomics !== "object") {
throw new Error("SharedArrayBuffer is unavailable. Serve Web builds with COOP/COEP headers.");
}
var source = [
"var files={};",
"self.onmessage=function(event){",
"var msg=event.data||{};",
"if(msg.kind==='register'){files[msg.fileId]=msg.file;return;}",
"if(msg.kind==='read'){",
"var header=new Int32Array(msg.sab,0,2);",
"var out=new Uint8Array(msg.sab,8);",
"try{",
"var file=files[msg.fileId];",
"if(!file)throw new Error('file handle is missing');",
"var bytes=new Uint8Array(new FileReaderSync().readAsArrayBuffer(file.slice(msg.offset,msg.offset+msg.length)));",
"out.set(bytes);",
"Atomics.store(header,1,bytes.length);",
"Atomics.store(header,0,1);",
"}catch(error){",
"Atomics.store(header,1,0);",
"Atomics.store(header,0,2);",
"}",
"Atomics.notify(header,0);",
"}",
"};"
].join("");
var workerUrl = URL.createObjectURL(new Blob([source], { type: "application/javascript" }));
store.readerWorker = new Worker(workerUrl);
URL.revokeObjectURL(workerUrl);
for (var fileId of Object.keys(store.files)) {
registerFileWithReader(fileId);
}
return store.readerWorker;
}

function registerFileWithReader(fileId) {
if (store.registeredFiles[fileId]) return;
if (!store.readerWorker) return;
var file = store.files[fileId];
if (!file) return;
store.readerWorker.postMessage({ kind: "register", fileId: fileId, file: file });
store.registeredFiles[fileId] = true;
}

function nowMs() {
return typeof performance === "object" && typeof performance.now === "function" ? performance.now() : Date.now();
}

function canUseAtomicsWait() {
if (atomicsWaitSupported !== null) return atomicsWaitSupported;
if (typeof window === "object" && typeof document === "object") {
atomicsWaitSupported = false;
return atomicsWaitSupported;
}
try {
var probe = new Int32Array(new SharedArrayBuffer(4));
Atomics.wait(probe, 0, 0, 0);
atomicsWaitSupported = true;
return atomicsWaitSupported;
} catch (e) {
atomicsWaitSupported = false;
return atomicsWaitSupported;
}
}

function waitForRead(header, timeoutMs) {
if (canUseAtomicsWait()) return Atomics.wait(header, 0, 0, timeoutMs);
var start = nowMs();
while (Atomics.load(header, 0) === 0) {
if (nowMs() - start > timeoutMs) return "timed-out";
}
return "ok";
}

function readLocalFileSync(fileId, offset, length) {
var file = store.files[fileId];
if (!file) throw new Error("Local file is no longer available");
if (typeof FileReaderSync === "function") {
return new Uint8Array(new FileReaderSync().readAsArrayBuffer(file.slice(offset, offset + length)));
}
var worker = ensureReaderWorker();
registerFileWithReader(fileId);
var sab = new SharedArrayBuffer(8 + length);
var header = new Int32Array(sab, 0, 2);
worker.postMessage({ kind: "read", fileId: fileId, offset: offset, length: length, sab: sab });
var waitResult = waitForRead(header, 30000);
if (waitResult === "timed-out") {
throw new Error("Local file read timed out");
}
if (Atomics.load(header, 0) !== 1) {
throw new Error("Local file read failed");
}
var count = Atomics.load(header, 1);
return new Uint8Array(sab, 8, count);
}

function canReadLocalFileSync() {
return typeof FileReaderSync === "function";
}

function cleanPath(path) {
return String(path || "").replace(/\\/g, "/").split("/").filter(Boolean).join("/");
}

function fileEntry(path, file) {
var fileId = nextId();
store.files[fileId] = file;
registerFileWithReader(fileId);
return {
path: cleanPath(path || file.name),
size: file.size || 0,
mtimeMs: file.lastModified || Date.now(),
fileId: fileId,
blobUrl: URL.createObjectURL(file)
};
}

function makeGame(name, type, files, options) {
options = options || {};
files = files.filter(function (file) { return file.path; }).sort(function (a, b) { return a.path.localeCompare(b.path); });
var id = options.id || stableGameId(name, type, files);
var mountPoint = "/webgames/" + id;
var archivePath = files.length > 0 ? files[0].path : "";
var gamePath = type === "Archive" ? mountPoint + "/" + archivePath : mountPoint;
store.games[id] = {
manifest: { files: files },
mountPoint: mountPoint,
name: name,
type: type
};
store.persistedGames[id] = { id: id, name: name, type: type, status: "available" };
if (options.persist) {
var persist = options.persist;
var record = {
id: id,
name: name,
type: type,
archivePath: archivePath,
handleKind: persist.handleKind || "",
directoryHandle: persist.directoryHandle || null,
fileHandle: persist.fileHandle || null,
persistedAt: Date.now()
};
writePersistedRecord(record);
}
return {
name: name,
path: gamePath,
type: type,
lastPlayed: 0,
playDurationSeconds: 0,
coverPath: "",
developer: "",
title: "",
webMountBackend: "blob",
webMountGameId: id,
webMountPoint: mountPoint
};
}

async function walkDirectoryHandle(handle, prefix, files) {
for await (var pair of handle.entries()) {
var name = pair[0];
var child = pair[1];
var path = prefix ? prefix + "/" + name : name;
if (child.kind === "directory") {
await walkDirectoryHandle(child, path, files);
} else if (child.kind === "file") {
files.push(fileEntry(path, await child.getFile()));
}
}
}

async function pickDirectoryWithHandle() {
var handle = await window.showDirectoryPicker({ mode: "read" });
await requestReadPermission(handle);
var files = [];
await walkDirectoryHandle(handle, "", files);
var existing = await findPersistedRecordForHandle("directory", handle);
return makeGame(handle.name || "Local Game", "Directory", files, {
id: existing && existing.id || "",
persist: { handleKind: "directory", directoryHandle: handle }
});
}

function pickFilesWithInput(kind) {
return new Promise(function (resolve, reject) {
var input = document.createElement("input");
input.type = "file";
input.style.position = "fixed";
input.style.left = "-10000px";
input.style.top = "-10000px";
if (kind === "directory") {
input.webkitdirectory = true;
input.multiple = true;
} else {
input.accept = ".xp3,.XP3";
}
input.addEventListener("change", function () {
var files = Array.prototype.slice.call(input.files || []);
input.remove();
if (files.length === 0) {
reject(new DOMException("No file selected", "AbortError"));
return;
}
resolve(files);
}, { once: true });
input.addEventListener("cancel", function () {
input.remove();
reject(new DOMException("Selection cancelled", "AbortError"));
}, { once: true });
document.body.appendChild(input);
input.click();
});
}

async function pickDirectoryWithInput() {
var inputFiles = await pickFilesWithInput("directory");
var rootName = "Local Game";
var files = [];
for (var file of inputFiles) {
var rel = cleanPath(file.webkitRelativePath || file.name);
var parts = rel.split("/").filter(Boolean);
if (parts.length > 1) {
rootName = rootName === "Local Game" ? parts[0] : rootName;
rel = parts.slice(1).join("/");
}
files.push(fileEntry(rel, file));
}
return makeGame(rootName, "Directory", files);
}

async function pickArchiveWithHandle() {
var handles = await window.showOpenFilePicker({
multiple: false,
excludeAcceptAllOption: false,
types: [{ description: "KiriKiri XP3 archive", accept: { "application/octet-stream": [".xp3"] } }]
});
await requestReadPermission(handles[0]);
var file = await handles[0].getFile();
var name = file.name.replace(/\.xp3$/i, "");
var existing = await findPersistedRecordForHandle("archive", handles[0]);
return makeGame(name, "Archive", [fileEntry(file.name, file)], {
id: existing && existing.id || "",
persist: { handleKind: "archive", fileHandle: handles[0] }
});
}

async function pickArchiveWithInput() {
var files = await pickFilesWithInput("archive");
var file = files[0];
var name = file.name.replace(/\.xp3$/i, "");
return makeGame(name, "Archive", [fileEntry(file.name, file)]);
}

async function pickLocalGame(kind) {
if (kind === "directory") {
if (typeof window.showDirectoryPicker === "function") return pickDirectoryWithHandle();
return pickDirectoryWithInput();
}
if (kind === "archive") {
if (typeof window.showOpenFilePicker === "function") return pickArchiveWithHandle();
return pickArchiveWithInput();
}
throw new Error("Unsupported picker kind: " + kind);
}

async function restorePersistedRecord(record) {
if (!record || !record.id) return;
store.persistedGames[record.id] = {
id: record.id,
name: record.name || "Local Game",
type: record.type || "Directory",
status: "restoring"
};
try {
if (record.handleKind === "directory" && record.directoryHandle) {
if (await queryReadPermission(record.directoryHandle) !== "granted") {
store.persistedGames[record.id].status = "permission";
return;
}
var files = [];
await walkDirectoryHandle(record.directoryHandle, "", files);
makeGame(record.name || record.directoryHandle.name || "Local Game", "Directory", files, { id: record.id });
return;
}
if (record.handleKind === "archive" && record.fileHandle) {
if (await queryReadPermission(record.fileHandle) !== "granted") {
store.persistedGames[record.id].status = "permission";
return;
}
var file = await record.fileHandle.getFile();
var name = (record.name || file.name || "Local Game").replace(/\.xp3$/i, "");
makeGame(name, "Archive", [fileEntry(record.archivePath || file.name, file)], { id: record.id });
return;
}
store.persistedGames[record.id].status = "unsupported";
} catch (error) {
store.persistedGames[record.id].status = "error";
store.persistedGames[record.id].error = String(error && error.message ? error.message : error);
}
}

function restorePersistedGames() {
if (localGameRestorePromise) return localGameRestorePromise;
localGameRestoreDone = false;
localGameRestorePromise = readPersistedRecords().then(async function (records) {
for (var i = 0; i < records.length; i++) {
var record = records[i];
if (record && record.id && !store.persistedGames[record.id]) {
store.persistedGames[record.id] = {
id: record.id,
name: record.name || "Local Game",
type: record.type || "Directory",
status: "queued"
};
}
}
for (var j = 0; j < records.length; j++) {
await restorePersistedRecord(records[j]);
}
localGameRestoreDone = true;
return records.length;
}).catch(function (error) {
localGameRestoreDone = true;
localGameRestoreError = String(error && error.message ? error.message : error);
return 0;
});
return localGameRestorePromise;
}

restorePersistedGames();

window.AetherKiriLocalPickerSupport = function () {
var probe = document.createElement("input");
return JSON.stringify({
directory: typeof window.showDirectoryPicker === "function" || "webkitdirectory" in probe,
archive: true,
fileSystemAccess: typeof window.showDirectoryPicker === "function" || typeof window.showOpenFilePicker === "function",
persistentLocalGames: canPersistHandles()
});
};

window.AetherKiriPickLocalGame = function (kind) {
var ticket = nextId();
store.picks[ticket] = { status: "pending" };
pickLocalGame(kind).then(function (game) {
store.picks[ticket] = { status: "ok", game: game };
}).catch(function (error) {
var name = error && error.name || "";
store.picks[ticket] = {
status: name === "AbortError" ? "cancelled" : "error",
error: String(error && error.message ? error.message : error)
};
});
return JSON.stringify({ ok: true, ticket: ticket });
};

window.AetherKiriTakeLocalGamePickResult = function (ticket) {
return JSON.stringify(store.picks[ticket] || { status: "missing", error: "Picker ticket was not found" });
};

window.AetherKiriLocalGameManifest = function (gameId) {
var game = store.games[gameId];
if (!game) throw new Error("Local game is no longer available. Please import it again.");
return JSON.stringify(game.manifest);
};

window.AetherKiriLocalGameAvailable = function (gameId) {
return !!store.games[gameId];
};

window.AetherKiriLocalGameRestoreState = function () {
var persisted = Object.keys(store.persistedGames || {});
var available = Object.keys(store.games || {});
return JSON.stringify({
done: localGameRestoreDone,
error: localGameRestoreError,
persisted: persisted,
available: available
});
};

window.AetherKiriReadLocalFileSync = function (fileId, offset, length) {
return readLocalFileSync(fileId, offset, length);
};
window.AetherKiriCanReadLocalFileSync = function () {
return canReadLocalFileSync();
};
})();
/* AETHERKIRI_LOCAL_PICKER_END */
