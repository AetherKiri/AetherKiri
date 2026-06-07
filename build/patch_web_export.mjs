#!/usr/bin/env node
import { copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const exportRoot = resolve(process.argv[2] ?? "");
if (!exportRoot) {
  throw new Error("Usage: node build/patch_web_export.mjs <export-root>");
}

const scriptDir = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDir, "..");
const jsPath = join(exportRoot, "index.js");
const htmlPath = join(exportRoot, "index.html");
const runtimeFontDir = join(exportRoot, "fonts");
const runtimeFonts = [
  {
    exportName: "default.ttf",
    required: true,
    source: join(projectRoot, "apps/godot_app/assets/fonts/aetherkiri-runtime-cjk.otf")
  },
  {
    exportName: "symbols.ttf",
    required: false,
    source: join(projectRoot, "apps/godot_app/assets/fonts/aetherkiri-runtime-symbols.ttf")
  }
];
const live2dRoot = join(projectRoot, "third_party/live2d");
const live2dCoreSourceDir = join(live2dRoot, "Core");
const live2dShaderSourceDir = join(live2dRoot, "CubismWebFramework/Shaders/WebGL");
const live2dBridgeConfig = join(projectRoot, "web/live2d/vite.config.ts");
const live2dBridgeBuiltPath = join(projectRoot, "out/live2d-bridge/aetherkiri-live2d-bridge.js");
const live2dExportDir = join(exportRoot, "live2d");
const live2dCoreExportDir = join(live2dExportDir, "Core");
const live2dShaderExportDir = join(live2dExportDir, "Shaders/WebGL");
const live2dBridgeExportPath = join(live2dExportDir, "aetherkiri-live2d-bridge.js");
const live2dCoreScriptCandidates = ["live2dcubismcore.min.js", "live2dcubismcore.js"];
let live2dCoreScriptName = "";
const rangeFsPatch = readFileSync(join(scriptDir, "web_export_range_fs.js"), "utf8");
const pickerPatch = readFileSync(join(scriptDir, "web_export_local_picker.js"), "utf8");
const rangeFsMarker = "/* AETHERKIRI_RANGE_FS */";
const rangeFsEndMarker = "/* AETHERKIRI_RANGE_FS_END */";
const pickerMarker = "/* AETHERKIRI_LOCAL_PICKER */";
const pickerEndMarker = "/* AETHERKIRI_LOCAL_PICKER_END */";
const fontPreloadMarker = "/* AETHERKIRI_WEB_FONT_PRELOAD */";
const clientLogMarker = "/* AETHERKIRI_CLIENT_LOG */";
const clientLogEndMarker = "/* AETHERKIRI_CLIENT_LOG_END */";
const live2dBridgeMarker = "<!-- AETHERKIRI_LIVE2D_BRIDGE -->";
const live2dBridgeEndMarker = "<!-- AETHERKIRI_LIVE2D_BRIDGE_END -->";
const wasmEhTagMarker = "/* AETHERKIRI_WASM_EH_TAGS */";
const wasmEhTagEndMarker = "/* AETHERKIRI_WASM_EH_TAGS_END */";
const clientLogPatch = `${clientLogMarker}
(function () {
  if (globalThis.__aetherKiriClientLogInstalled) return;
  globalThis.__aetherKiriClientLogInstalled = true;
  function formatArg(value) {
    try {
      if (value instanceof Error) return value.stack || value.message;
      if (typeof value === "string") return value;
      return JSON.stringify(value);
    } catch (_) {
      return String(value);
    }
  }
  function post(level, args) {
    try {
      var message = Array.prototype.map.call(args, formatArg).join(" ");
      if (message.length > 8000) message = message.slice(0, 8000);
      fetch("/__aetherkiri/client-log", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          level: level,
          message: message,
          timestamp: new Date().toISOString()
        }),
        keepalive: true
      }).catch(function () {});
    } catch (_) {}
  }
  ["debug", "error", "info", "log", "warn"].forEach(function (level) {
    var original = console[level];
    console[level] = function () {
      post(level, arguments);
      return original.apply(this, arguments);
    };
  });
})();
${clientLogEndMarker}`;
const wasmEhTagPatch = `${wasmEhTagMarker}
if (typeof WebAssembly.Tag === "function") {
  wasmImports.__cpp_exception ??= new WebAssembly.Tag({ parameters: ["i32"] });
  wasmImports.__c_longjmp ??= new WebAssembly.Tag({ parameters: ["i32"] });
}
if (!GOT.__wasm_lpad_context || GOT.__wasm_lpad_context.value === -1) {
  GOT.__wasm_lpad_context = new WebAssembly.Global({ value: "i32", mutable: true }, getMemory(12));
}
${wasmEhTagEndMarker}`;
const emscriptenPoolSize = Number.parseInt(process.env.AETHERKIRI_WEB_EMSCRIPTEN_POOL_SIZE ?? "32", 10);
const godotPoolSize = Number.parseInt(process.env.AETHERKIRI_WEB_GODOT_POOL_SIZE ?? "8", 10);

function replaceMarkedBlock(source, startMarker, endMarker, replacement) {
  const start = source.indexOf(startMarker);
  if (start === -1) {
    return null;
  }
  const end = source.indexOf(endMarker, start);
  if (end === -1) {
    throw new Error(`Found ${startMarker} without ${endMarker}. Re-export the Web build once to replace the legacy injected block.`);
  }
  return source.slice(0, start) + replacement + source.slice(end + endMarker.length);
}

function enableIdbfsAutoPersist(source) {
  const target = "FS.mount(IDBFS,{},path)";
  const replacement = "FS.mount(IDBFS,{autoPersist:true},path)";
  if (source.includes(replacement)) {
    return source;
  }
  if (!source.includes(target)) {
    throw new Error(`Unable to locate Godot IDBFS mount in ${jsPath}`);
  }
  return source.replace(target, replacement);
}

function buildAndCopyLive2D() {
  mkdirSync(live2dExportDir, { recursive: true });

  if (existsSync(live2dBridgeConfig)) {
    const viteBin = join(projectRoot, "node_modules/.bin/vite");
    if (!existsSync(viteBin)) {
      console.warn("[aetherkiri] Vite was not found; Live2D bridge was not built.");
    } else {
      execFileSync(viteBin, ["build", "--config", live2dBridgeConfig], {
        cwd: projectRoot,
        stdio: "inherit"
      });
      if (existsSync(live2dBridgeBuiltPath)) {
        copyFileSync(live2dBridgeBuiltPath, live2dBridgeExportPath);
      } else {
        console.warn(`[aetherkiri] Live2D bridge build output missing: ${live2dBridgeBuiltPath}`);
      }
    }
  }

  if (existsSync(live2dShaderSourceDir)) {
    rmSync(live2dShaderExportDir, { force: true, recursive: true });
    mkdirSync(dirname(live2dShaderExportDir), { recursive: true });
    cpSync(live2dShaderSourceDir, live2dShaderExportDir, { recursive: true });
  }

  mkdirSync(live2dCoreExportDir, { recursive: true });
  let copiedCore = false;
  for (const name of [...live2dCoreScriptCandidates, "live2dcubismcore.js.map"]) {
    const source = join(live2dCoreSourceDir, name);
    if (existsSync(source)) {
      copyFileSync(source, join(live2dCoreExportDir, name));
      if (live2dCoreScriptCandidates.includes(name)) {
        copiedCore = true;
        live2dCoreScriptName ||= name;
      }
    }
  }
  if (!copiedCore) {
    console.warn("[aetherkiri] Live2D Cubism Core for Web was not found. Put live2dcubismcore.min.js in third_party/live2d/Core to enable Live2D rendering.");
  }
}

buildAndCopyLive2D();

let js = readFileSync(jsPath, "utf8");
const replacedJs = replaceMarkedBlock(js, rangeFsMarker, rangeFsEndMarker, rangeFsPatch);
if (replacedJs != null) {
  js = replacedJs;
} else {
  const target = 'Module["copyToFS"]=GodotFS.copy_to_fs;';
  if (!js.includes(target)) {
    throw new Error(`Unable to locate GodotFS copy_to_fs hook in ${jsPath}`);
  }
  js = js.replace(target, `${target}\n${rangeFsPatch}`);
}
const replacedWasmEhTagPatch = replaceMarkedBlock(js, wasmEhTagMarker, wasmEhTagEndMarker, wasmEhTagPatch);
if (replacedWasmEhTagPatch != null) {
  js = replacedWasmEhTagPatch;
} else {
  const target = 'var proxyHandler={get(stubs,prop){switch(prop){case"__memory_base":return memoryBase;case"__table_base":return tableBase}';
  if (!js.includes(target)) {
    throw new Error(`Unable to locate dynamic library import proxy in ${jsPath}`);
  }
  js = js.replace(target, `${wasmEhTagPatch}\n${target}`);
}
if (Number.isFinite(emscriptenPoolSize) && emscriptenPoolSize > 0) {
  js = js.replace(/emscriptenPoolSize:\s*\d+/, `emscriptenPoolSize: ${emscriptenPoolSize}`);
}
if (Number.isFinite(godotPoolSize) && godotPoolSize > 0) {
  js = js.replace(/godotPoolSize:\s*\d+/, `godotPoolSize: ${godotPoolSize}`);
}
js = enableIdbfsAutoPersist(js);
writeFileSync(jsPath, js);

mkdirSync(runtimeFontDir, { recursive: true });
for (const font of runtimeFonts) {
  if (!existsSync(font.source)) {
    if (font.required) {
      throw new Error(`Runtime font not found: ${font.source}`);
    }
    console.warn(`[aetherkiri] Optional runtime font not found: ${font.source}`);
    continue;
  }
  copyFileSync(font.source, join(runtimeFontDir, font.exportName));
}

let html = readFileSync(htmlPath, "utf8");

const engineLine = "const engine = new Engine(GODOT_CONFIG);";
const engineExposeLine = "window.aetherKiriEngine = engine;";
if (!html.includes(engineExposeLine)) {
  if (!html.includes(engineLine)) {
    throw new Error(`Unable to locate Godot Engine construction in ${htmlPath}`);
  }
  html = html.replace(engineLine, `${engineLine}\n${engineExposeLine}`);
}
const replacedClientLog = replaceMarkedBlock(html, clientLogMarker, clientLogEndMarker, clientLogPatch);
if (replacedClientLog != null) {
  html = replacedClientLog;
} else {
  html = html.replace(engineExposeLine, `${engineExposeLine}\n${clientLogPatch}`);
}
const replacedHtml = replaceMarkedBlock(html, pickerMarker, pickerEndMarker, pickerPatch);
if (replacedHtml != null) {
  html = replacedHtml;
} else {
  html = html.replace(engineExposeLine, `${engineExposeLine}\n${pickerPatch}`);
}
const live2dCoreScript = live2dCoreScriptName
  ? `\n\t<script src="live2d/Core/${live2dCoreScriptName}"></script>`
  : "";
const live2dBridgePatch = `${live2dBridgeMarker}${live2dCoreScript}
\t<script src="live2d/aetherkiri-live2d-bridge.js"></script>
\t${live2dBridgeEndMarker}`;
const replacedLive2D = replaceMarkedBlock(html, live2dBridgeMarker, live2dBridgeEndMarker, live2dBridgePatch);
if (replacedLive2D != null) {
  html = replacedLive2D;
} else if (html.includes("</body>")) {
  html = html.replace("</body>", `${live2dBridgePatch}\n</body>`);
} else {
  html += `\n${live2dBridgePatch}\n`;
}
if (Number.isFinite(emscriptenPoolSize) && emscriptenPoolSize > 0) {
  html = html.replace(/"emscriptenPoolSize":\d+/, `"emscriptenPoolSize":${emscriptenPoolSize}`);
}
if (Number.isFinite(godotPoolSize) && godotPoolSize > 0) {
  html = html.replace(/"godotPoolSize":\d+/, `"godotPoolSize":${godotPoolSize}`);
}
if (!html.includes(fontPreloadMarker)) {
  const startGameCall = "\t\tengine.startGame({";
  const startGameThen = "\t\t}).then(() => {";
  const start = html.indexOf(startGameCall);
  if (start === -1) {
    throw new Error(`Unable to locate Godot startGame call in ${htmlPath}`);
  }
  const thenIndex = html.indexOf(startGameThen, start);
  if (thenIndex === -1) {
    throw new Error(`Unable to locate Godot startGame continuation in ${htmlPath}`);
  }
  html = html.slice(0, start)
    + `\t\t${fontPreloadMarker}\n`
    + "\t\tPromise.all([engine.preloadFile('fonts/default.ttf', '/fonts/default.ttf'), engine.preloadFile('fonts/symbols.ttf', '/fonts/symbols.ttf')]).then(() => engine.startGame({"
    + html.slice(start + startGameCall.length, thenIndex)
    + "\t\t})).then(() => {"
    + html.slice(thenIndex + startGameThen.length);
} else {
  html = html.replace(
    "engine.preloadFile('fonts/default.ttf', '/fonts/default.ttf').then(() => engine.startGame({",
    "Promise.all([engine.preloadFile('fonts/default.ttf', '/fonts/default.ttf'), engine.preloadFile('fonts/symbols.ttf', '/fonts/symbols.ttf')]).then(() => engine.startGame({"
  );
}
writeFileSync(htmlPath, html);
