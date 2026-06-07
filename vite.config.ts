import { createHash } from "node:crypto";
import {
  createReadStream,
  existsSync,
  lstatSync,
  readdirSync,
  statSync
} from "node:fs";
import type { IncomingMessage, ServerResponse } from "node:http";
import {
  basename,
  delimiter,
  relative,
  resolve,
  sep
} from "node:path";
import { defineConfig } from "vite";

const webDir = process.env.AETHERKIRI_WEB_DIR ?? "out/godot/web/debug";
const root = resolve(process.cwd(), webDir);
const port = Number.parseInt(process.env.PORT ?? "8060", 10);
const gameMountPrefix = "/__aetherkiri/game/";

if (!existsSync(resolve(root, "index.html"))) {
  console.warn(
    `[aetherkiri] ${webDir}/index.html was not found. Run ./build.sh web debug or ./build.sh web release first.`
  );
}

const isolationHeaders: Record<string, string> = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Resource-Policy": "same-origin"
};

type GameRoot = {
  baseUrl: string;
  gamePath: string;
  id: string;
  mountPoint: string;
  name: string;
  rootPath: string;
  type: "Archive" | "Directory";
};

type ManifestFile = {
  mtimeMs: number;
  path: string;
  size: number;
};

type DevConfig = {
  autoStartGame: boolean;
  autoStartIndex: number;
  autoStartName: string;
};

type ClientLogEntry = {
  level: string;
  message: string;
  timestamp: string;
};

const clientLogs: ClientLogEntry[] = [];
const maxClientLogs = 5000;

function envFlag(...names: string[]): boolean {
  for (const name of names) {
    const value = (process.env[name] ?? "").trim().toLowerCase();
    if (value.length === 0) {
      continue;
    }
    return ["1", "true", "yes", "on"].includes(value);
  }
  return false;
}

function envString(...names: string[]): string {
  for (const name of names) {
    const value = (process.env[name] ?? "").trim();
    if (value.length > 0) {
      return value;
    }
  }
  return "";
}

function nonNegativeEnvInt(name: string, fallback: number): number {
  const parsed = Number.parseInt(process.env[name] ?? "", 10);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : fallback;
}

function devConfig(): DevConfig {
  const autoStartName = envString("AETHERKIRI_WEB_AUTO_START_NAME");
  const autoStartIndex = nonNegativeEnvInt("AETHERKIRI_WEB_AUTO_START_INDEX", 0);
  const autoStartGame =
    envFlag("AETHERKIRI_WEB_AUTO_START_GAME", "AETHERKIRI_WEB_AUTO_START", "AETHERKIRI_AUTO_OPEN") ||
    autoStartName.length > 0 ||
    process.env.AETHERKIRI_WEB_AUTO_START_INDEX != null;
  return {
    autoStartGame,
    autoStartIndex,
    autoStartName
  };
}

function sendJson(response: ServerResponse, value: unknown): void {
  response.statusCode = 200;
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  response.end(JSON.stringify(value));
}

function sendError(response: ServerResponse, statusCode: number, message: string): void {
  response.statusCode = statusCode;
  response.setHeader("Content-Type", "text/plain; charset=utf-8");
  response.end(message);
}

function readRequestBody(
  request: IncomingMessage,
  maxBytes: number,
  done: (body: string | null) => void
): void {
  let body = "";
  let settled = false;
  const finish = (value: string | null) => {
    if (settled) {
      return;
    }
    settled = true;
    done(value);
  };
  request.setEncoding("utf8");
  request.on("data", (chunk: string) => {
    body += chunk;
    if (body.length > maxBytes) {
      body = body.slice(0, maxBytes);
      request.destroy();
    }
  });
  request.on("end", () => finish(body));
  request.on("error", () => finish(null));
  request.on("close", () => {
    if (!request.complete) {
      finish(null);
    }
  });
}

function pushClientLog(value: unknown): void {
  if (value == null || typeof value !== "object") {
    return;
  }
  const record = value as Partial<ClientLogEntry>;
  const level = String(record.level ?? "log").slice(0, 16);
  const message = String(record.message ?? "").slice(0, 8000);
  if (message.length === 0) {
    return;
  }
  const timestamp = String(record.timestamp ?? new Date().toISOString()).slice(0, 64);
  clientLogs.push({ level, message, timestamp });
  while (clientLogs.length > maxClientLogs) {
    clientLogs.shift();
  }
}

function configuredGameRoots(): GameRoot[] {
  const raw = [
    process.env.AETHERKIRI_GAME_ROOT ?? "",
    process.env.AETHERKIRI_GAME_ROOTS ?? ""
  ]
    .filter((value) => value.length > 0)
    .join(delimiter);
  const paths = raw
    .split(delimiter)
    .map((value) => value.trim())
    .filter((value) => value.length > 0);
  const seen = new Set<string>();
  const roots: GameRoot[] = [];
  for (const inputPath of paths) {
    const rootPath = resolve(inputPath);
    if (seen.has(rootPath) || !existsSync(rootPath)) {
      continue;
    }
    const stat = statSync(rootPath);
    if (!stat.isDirectory() && !rootPath.toLowerCase().endsWith(".xp3")) {
      continue;
    }
    seen.add(rootPath);
    const id = createHash("sha1").update(rootPath).digest("hex").slice(0, 12);
    const name = basename(rootPath).replace(/\.xp3$/i, "");
    const type = stat.isDirectory() ? "Directory" : "Archive";
    const mountPoint = `/webgames/${id}`;
    roots.push({
      baseUrl: `${gameMountPrefix}${id}`,
      gamePath: type === "Archive" ? `${mountPoint}/${basename(rootPath)}` : mountPoint,
      id,
      mountPoint,
      name,
      rootPath,
      type
    });
  }
  return roots;
}

function findRoot(id: string): GameRoot | undefined {
  return configuredGameRoots().find((game) => game.id === id);
}

function isInsideRoot(rootPath: string, filePath: string): boolean {
  const rel = relative(rootPath, filePath);
  return rel.length === 0 || (!rel.startsWith("..") && !rel.startsWith(sep));
}

function manifestForGame(game: GameRoot): ManifestFile[] {
  const rootStat = statSync(game.rootPath);
  if (!rootStat.isDirectory()) {
    return [{
      mtimeMs: rootStat.mtimeMs,
      path: basename(game.rootPath),
      size: rootStat.size
    }];
  }

  const files: ManifestFile[] = [];
  const pending = [game.rootPath];
  while (pending.length > 0) {
    const current = pending.pop();
    if (current == null) {
      break;
    }
    for (const entry of readdirSync(current)) {
      if (entry === "." || entry === "..") {
        continue;
      }
      const fullPath = resolve(current, entry);
      const entryStat = lstatSync(fullPath);
      if (entryStat.isSymbolicLink()) {
        continue;
      }
      if (entryStat.isDirectory()) {
        pending.push(fullPath);
        continue;
      }
      if (!entryStat.isFile()) {
        continue;
      }
      files.push({
        mtimeMs: entryStat.mtimeMs,
        path: relative(game.rootPath, fullPath).split(sep).join("/"),
        size: entryStat.size
      });
    }
  }
  files.sort((a, b) => a.path.localeCompare(b.path));
  return files;
}

function resolveGameFile(game: GameRoot, relativePath: string): string | null {
  if (statSync(game.rootPath).isFile()) {
    return relativePath === basename(game.rootPath) ? game.rootPath : null;
  }
  const fullPath = resolve(game.rootPath, relativePath);
  if (!isInsideRoot(game.rootPath, fullPath) || !existsSync(fullPath)) {
    return null;
  }
  const entryStat = statSync(fullPath);
  return entryStat.isFile() ? fullPath : null;
}

function serveGameFile(
  requestUrl: URL,
  response: ServerResponse,
  game: GameRoot
): void {
  const relativePath = requestUrl.searchParams.get("path") ?? "";
  const filePath = resolveGameFile(game, relativePath);
  if (filePath == null) {
    sendError(response, 404, "Game file not found");
    return;
  }
  const fileStat = statSync(filePath);
  const offset = Math.max(0, Number.parseInt(requestUrl.searchParams.get("offset") ?? "0", 10));
  const requestedLength = Math.max(0, Number.parseInt(requestUrl.searchParams.get("length") ?? "1048576", 10));
  const maxLength = 16 * 1024 * 1024;
  const length = Math.min(requestedLength, maxLength, Math.max(0, fileStat.size - offset));
  if (!Number.isFinite(offset) || !Number.isFinite(length) || offset > fileStat.size) {
    sendError(response, 416, "Invalid range");
    return;
  }
  response.statusCode = 206;
  response.setHeader("Content-Type", "application/octet-stream");
  response.setHeader("Content-Length", String(length));
  response.setHeader("Content-Range", `bytes ${offset}-${offset + Math.max(0, length - 1)}/${fileStat.size}`);
  if (length === 0) {
    response.end();
    return;
  }
  createReadStream(filePath, { start: offset, end: offset + length - 1 }).pipe(response);
}

function aetherKiriGameMiddleware(
  request: IncomingMessage,
  response: ServerResponse,
  next: () => void
): void {
  if (request.url == null) {
    next();
    return;
  }
  const requestUrl = new URL(request.url, "http://127.0.0.1");
  if (requestUrl.pathname.endsWith(".pck")) {
    response.setHeader("Content-Type", "application/octet-stream");
  }
  if (requestUrl.pathname === "/__aetherkiri/client-log" && request.method === "POST") {
    readRequestBody(request, 128 * 1024, (body) => {
      if (body != null) {
        try {
          const parsed = JSON.parse(body);
          if (Array.isArray(parsed)) {
            for (const item of parsed) {
              pushClientLog(item);
            }
          } else {
            pushClientLog(parsed);
          }
        } catch {
          pushClientLog({
            level: "log",
            message: body,
            timestamp: new Date().toISOString()
          });
        }
      }
      sendJson(response, { ok: true });
    });
    return;
  }
  if (requestUrl.pathname === "/__aetherkiri/client-logs") {
    const logs = clientLogs.slice();
    if (requestUrl.searchParams.get("clear") === "1") {
      clientLogs.length = 0;
    }
    sendJson(response, { logs });
    return;
  }
  if (requestUrl.pathname === "/__aetherkiri/config") {
    sendJson(response, devConfig());
    return;
  }
  if (requestUrl.pathname === "/__aetherkiri/games") {
    sendJson(response, {
      games: configuredGameRoots().map(({ rootPath: _rootPath, ...game }) => game)
    });
    return;
  }
  if (!requestUrl.pathname.startsWith(gameMountPrefix)) {
    next();
    return;
  }
  const parts = requestUrl.pathname.slice(gameMountPrefix.length).split("/");
  const id = parts[0] ?? "";
  const action = parts[1] ?? "";
  const game = findRoot(id);
  if (game == null) {
    sendError(response, 404, "Game root is not configured");
    return;
  }
  if (action === "manifest") {
    sendJson(response, { files: manifestForGame(game) });
    return;
  }
  if (action === "file") {
    serveGameFile(requestUrl, response, game);
    return;
  }
  sendError(response, 404, "Unknown game mount endpoint");
}

export default defineConfig({
  root,
  appType: "mpa",
  cacheDir: resolve(process.cwd(), "node_modules/.vite"),
  plugins: [{
    name: "aetherkiri-game-mounts",
    configureServer(server) {
      server.middlewares.use(aetherKiriGameMiddleware);
    }
  }],
  server: {
    host: "127.0.0.1",
    port,
    strictPort: false,
    headers: isolationHeaders,
    fs: {
      strict: true,
      allow: [root]
    }
  }
});
