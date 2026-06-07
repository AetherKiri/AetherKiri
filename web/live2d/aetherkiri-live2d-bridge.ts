import { CubismModelSettingJson } from "../../third_party/live2d/CubismWebFramework/src/cubismmodelsettingjson";
import {
  CubismFramework,
  LogLevel,
  Option
} from "../../third_party/live2d/CubismWebFramework/src/live2dcubismframework";
import { CubismMatrix44 } from "../../third_party/live2d/CubismWebFramework/src/math/cubismmatrix44";
import { CubismUserModel } from "../../third_party/live2d/CubismWebFramework/src/model/cubismusermodel";
import { CubismMotion } from "../../third_party/live2d/CubismWebFramework/src/motion/cubismmotion";

declare global {
  interface Window {
    AetherKiriLive2D?: Live2DBridge;
    __aetherKiriLive2DCoreStub?: boolean;
    Live2DCubismCore?: unknown;
    __aetherKiriPendingLive2D?: PendingLoad[];
  }

  // Provided by the proprietary Core runtime at live2dcubismcore*.js.
  // eslint-disable-next-line no-var
  var Live2DCubismCore: unknown;
}

type ZipEntries = Map<string, Uint8Array>;

type PendingLoad = {
  bytes: Uint8Array;
  id: number;
  storage: string;
};

type RuntimeTexture = {
  id: WebGLTexture;
  width: number;
  height: number;
};

const CORE_URLS = [
  "/live2d/Core/live2dcubismcore.min.js",
  "/live2d/Core/live2dcubismcore.js"
];
const SHADER_PATH = "/live2d/Shaders/WebGL/";
const KTX_IDENTIFIER = [
  0xab, 0x4b, 0x54, 0x58, 0x20, 0x31, 0x31, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a
];
const GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8e8c;

let frameworkReady = false;

function log(...args: unknown[]): void {
  console.log("[AetherKiri Live2D]", ...args);
}

function warn(...args: unknown[]): void {
  console.warn("[AetherKiri Live2D]", ...args);
}

function readU16(view: DataView, offset: number): number {
  return view.getUint16(offset, true);
}

function readU32(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

function text(bytes: Uint8Array): string {
  return new TextDecoder("utf-8").decode(bytes);
}

function toArrayBuffer(bytes: Uint8Array): ArrayBuffer {
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function normalizePath(path: string): string {
  return path.replace(/\\/g, "/").replace(/^\.?\//, "").toLowerCase();
}

function withKtxExtension(path: string): string {
  const normalized = path.replace(/\\/g, "/");
  const dot = normalized.lastIndexOf(".");
  return dot >= 0 ? `${normalized.slice(0, dot)}.ktx` : `${normalized}.ktx`;
}

function findEntry(entries: ZipEntries, candidates: string[]): Uint8Array | null {
  for (const candidate of candidates) {
    const direct = entries.get(normalizePath(candidate));
    if (direct) {
      return direct;
    }
  }

  for (const candidate of candidates) {
    const wanted = normalizePath(candidate).split("/").pop();
    if (!wanted) {
      continue;
    }
    for (const [name, data] of entries) {
      if (name.split("/").pop() === wanted) {
        return data;
      }
    }
  }
  return null;
}

async function inflateRaw(bytes: Uint8Array): Promise<Uint8Array> {
  if (typeof DecompressionStream !== "function") {
    throw new Error("DecompressionStream is unavailable; cannot inflate Live2D l2d zip");
  }
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function unzip(bytes: Uint8Array): Promise<ZipEntries> {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let eocd = -1;
  for (let offset = bytes.byteLength - 22; offset >= 0; --offset) {
    if (readU32(view, offset) === 0x06054b50) {
      eocd = offset;
      break;
    }
  }
  if (eocd < 0) {
    throw new Error("Live2D l2d zip has no EOCD record");
  }

  const entryCount = readU16(view, eocd + 10);
  let centralOffset = readU32(view, eocd + 16);
  const entries: ZipEntries = new Map();

  for (let i = 0; i < entryCount; ++i) {
    if (readU32(view, centralOffset) !== 0x02014b50) {
      throw new Error("Invalid ZIP central directory entry");
    }
    const method = readU16(view, centralOffset + 10);
    const compressedSize = readU32(view, centralOffset + 20);
    const fileNameLength = readU16(view, centralOffset + 28);
    const extraLength = readU16(view, centralOffset + 30);
    const commentLength = readU16(view, centralOffset + 32);
    const localOffset = readU32(view, centralOffset + 42);
    const fileName = text(bytes.subarray(centralOffset + 46, centralOffset + 46 + fileNameLength));

    const localNameLength = readU16(view, localOffset + 26);
    const localExtraLength = readU16(view, localOffset + 28);
    const dataStart = localOffset + 30 + localNameLength + localExtraLength;
    const compressed = bytes.subarray(dataStart, dataStart + compressedSize);

    let data: Uint8Array;
    if (method === 0) {
      data = compressed.slice();
    } else if (method === 8) {
      data = await inflateRaw(compressed);
    } else {
      throw new Error(`Unsupported ZIP compression method ${method} for ${fileName}`);
    }

    entries.set(normalizePath(fileName), data);
    centralOffset += 46 + fileNameLength + extraLength + commentLength;
  }
  return entries;
}

async function loadScript(url: string): Promise<boolean> {
  return new Promise((resolve) => {
    const script = document.createElement("script");
    script.async = true;
    script.src = url;
    script.onload = () => resolve(true);
    script.onerror = () => {
      script.remove();
      resolve(false);
    };
    document.head.appendChild(script);
  });
}

function hasRealCore(): boolean {
  const core = window.Live2DCubismCore || globalThis.Live2DCubismCore;
  return !!core && !window.__aetherKiriLive2DCoreStub && !(core as { __aetherKiriCoreStub?: boolean }).__aetherKiriCoreStub;
}

async function ensureCore(): Promise<boolean> {
  if (hasRealCore()) {
    return true;
  }
  if (window.__aetherKiriLive2DCoreStub || (globalThis.Live2DCubismCore as { __aetherKiriCoreStub?: boolean } | undefined)?.__aetherKiriCoreStub) {
    delete window.Live2DCubismCore;
    delete globalThis.Live2DCubismCore;
    window.__aetherKiriLive2DCoreStub = false;
  }
  for (const url of CORE_URLS) {
    if (await loadScript(url)) {
      if (hasRealCore()) {
        return true;
      }
    }
  }
  warn("Cubism Core for Web is missing. Put live2dcubismcore.min.js under third_party/live2d/Core and rebuild.");
  return false;
}

async function ensureFramework(): Promise<boolean> {
  if (!(await ensureCore())) {
    return false;
  }
  if (!frameworkReady) {
    const option = new Option();
    option.logFunction = (message: string) => log(message);
    option.loggingLevel = LogLevel.LogLevel_Warning;
    CubismFramework.startUp(option);
    CubismFramework.initialize(64 * 1024 * 1024);
    frameworkReady = true;
  }
  return true;
}

function makeCanvas(): HTMLCanvasElement {
  let canvas = document.getElementById("aetherkiri-live2d-canvas") as HTMLCanvasElement | null;
  if (!canvas) {
    canvas = document.createElement("canvas");
    canvas.id = "aetherkiri-live2d-canvas";
    canvas.style.position = "fixed";
    canvas.style.inset = "0";
    canvas.style.width = "100vw";
    canvas.style.height = "100vh";
    canvas.style.pointerEvents = "none";
    canvas.style.zIndex = "7";
    canvas.style.display = "none";
    document.body.appendChild(canvas);
  }
  const dpr = Math.max(1, Math.min(window.devicePixelRatio || 1, 2));
  const width = Math.max(1, Math.floor(window.innerWidth * dpr));
  const height = Math.max(1, Math.floor(window.innerHeight * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  return canvas;
}

function createWhiteTexture(gl: WebGLRenderingContext | WebGL2RenderingContext): RuntimeTexture {
  const texture = gl.createTexture();
  if (!texture) {
    throw new Error("Failed to create WebGL texture");
  }
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(
    gl.TEXTURE_2D,
    0,
    gl.RGBA,
    1,
    1,
    0,
    gl.RGBA,
    gl.UNSIGNED_BYTE,
    new Uint8Array([255, 255, 255, 255])
  );
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return { id: texture, width: 1, height: 1 };
}

function loadKtxTexture(
  gl: WebGLRenderingContext | WebGL2RenderingContext,
  bytes: Uint8Array
): RuntimeTexture | null {
  if (bytes.byteLength < 68) {
    return null;
  }
  for (let i = 0; i < KTX_IDENTIFIER.length; ++i) {
    if (bytes[i] !== KTX_IDENTIFIER[i]) {
      return null;
    }
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const internalFormat = readU32(view, 28);
  const width = readU32(view, 36);
  const height = readU32(view, 40);
  const mipLevels = Math.max(1, readU32(view, 56));
  const keyValueBytes = readU32(view, 60);

  if (internalFormat === GL_COMPRESSED_RGBA_BPTC_UNORM) {
    const extension = gl.getExtension("EXT_texture_compression_bptc");
    if (!extension) {
      warn("KTX texture is BC7/BPTC but EXT_texture_compression_bptc is unavailable");
      return null;
    }
  }

  const texture = gl.createTexture();
  if (!texture) {
    return null;
  }
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);

  let offset = 64 + keyValueBytes;
  let mipWidth = width;
  let mipHeight = height;
  for (let level = 0; level < mipLevels && offset + 4 <= bytes.byteLength; ++level) {
    const imageSize = readU32(view, offset);
    offset += 4;
    const image = bytes.subarray(offset, offset + imageSize);
    if (internalFormat === GL_COMPRESSED_RGBA_BPTC_UNORM) {
      gl.compressedTexImage2D(gl.TEXTURE_2D, level, internalFormat, mipWidth, mipHeight, 0, image);
    } else {
      gl.texImage2D(gl.TEXTURE_2D, level, internalFormat, mipWidth, mipHeight, 0, gl.RGBA, gl.UNSIGNED_BYTE, image);
    }
    offset += imageSize;
    offset = (offset + 3) & ~3;
    mipWidth = Math.max(1, mipWidth >> 1);
    mipHeight = Math.max(1, mipHeight >> 1);
  }

  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mipLevels > 1 ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return { id: texture, width, height };
}

async function loadImageTexture(
  gl: WebGLRenderingContext | WebGL2RenderingContext,
  bytes: Uint8Array
): Promise<RuntimeTexture | null> {
  const bitmap = await createImageBitmap(new Blob([bytes]));
  const texture = gl.createTexture();
  if (!texture) {
    bitmap.close();
    return null;
  }
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, bitmap);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.bindTexture(gl.TEXTURE_2D, null);
  const result = { id: texture, width: bitmap.width, height: bitmap.height };
  bitmap.close();
  return result;
}

class RuntimeModel extends CubismUserModel {
  private entries: ZipEntries = new Map();
  private gl: WebGLRenderingContext | WebGL2RenderingContext;
  private lastTime = performance.now();
  private motions = new Map<string, CubismMotion>();
  private ready = false;
  public visible = true;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    gl: WebGLRenderingContext | WebGL2RenderingContext,
    private readonly id: number,
    private readonly storage: string
  ) {
    super();
    this.gl = gl;
  }

  async load(bytes: Uint8Array): Promise<void> {
    this.entries = await unzip(bytes);
    const base = this.storage.replace(/\\/g, "/").replace(/\.[^.]+$/, "");
    const modelJson =
      findEntry(this.entries, [`${base}.model3.json`]) ??
      [...this.entries.entries()].find(([name]) => name.endsWith(".model3.json"))?.[1] ??
      null;
    if (!modelJson) {
      throw new Error(`No model3.json in ${this.storage}`);
    }

    const setting = new CubismModelSettingJson(toArrayBuffer(modelJson), modelJson.byteLength);
    const mocName = setting.getModelFileName() || `${base}.moc3`;
    const moc = findEntry(this.entries, [mocName, `${base}.moc3`]);
    if (!moc) {
      throw new Error(`No moc3 in ${this.storage}`);
    }

    this.loadModel(toArrayBuffer(moc), false);
    const layout = new Map<string, number>();
    setting.getLayoutMap(layout);
    this._modelMatrix.setupFromLayout(layout);

    this.createRenderer(this.canvas.width, this.canvas.height);
    this.getRenderer().startUp(this.gl);
    this.getRenderer().setIsPremultipliedAlpha(false);
    this.getRenderer().loadShaders(SHADER_PATH);

    const textureCount = setting.getTextureCount();
    for (let i = 0; i < textureCount; ++i) {
      const texturePath = setting.getTextureFileName(i);
      if (!texturePath) {
        continue;
      }
      const textureBytes = findEntry(this.entries, [texturePath, withKtxExtension(texturePath)]);
      let texture: RuntimeTexture | null = null;
      if (textureBytes) {
        texture = loadKtxTexture(this.gl, textureBytes);
        if (!texture) {
          try {
            texture = await loadImageTexture(this.gl, textureBytes);
          } catch (error) {
            warn("Texture decode failed", texturePath, error);
          }
        }
      }
      texture ??= createWhiteTexture(this.gl);
      this.getRenderer().bindTexture(i, texture.id);
    }

    for (let g = 0; g < setting.getMotionGroupCount(); ++g) {
      const group = setting.getMotionGroupName(g);
      for (let i = 0; i < setting.getMotionCount(group); ++i) {
        const motionName = setting.getMotionFileName(group, i);
        const motionBytes = findEntry(this.entries, [motionName]);
        if (!motionBytes) {
          continue;
        }
        const key = `${group}_${i}`;
        const motion = this.loadMotion(toArrayBuffer(motionBytes), motionBytes.byteLength, key, undefined, undefined, setting, group, i, false);
        if (motion) {
          this.motions.set(key, motion);
        }
      }
    }

    this.ready = true;
    const firstMotion = this.motions.values().next().value as CubismMotion | undefined;
    if (firstMotion) {
      this._motionManager.startMotionPriority(firstMotion, false, 1);
    }
    log(`model loaded id=${this.id} storage=${this.storage} textures=${textureCount} motions=${this.motions.size}`);
  }

  draw(): void {
    if (!this.ready || !this.visible || this._model == null) {
      return;
    }
    const now = performance.now();
    let dt = (now - this.lastTime) / 1000;
    this.lastTime = now;
    if (!Number.isFinite(dt) || dt <= 0) {
      dt = 1 / 60;
    }
    dt = Math.min(dt, 0.1);

    this._model.loadParameters();
    if (this._motionManager.isFinished() && this.motions.size > 0) {
      const firstMotion = this.motions.values().next().value as CubismMotion;
      this._motionManager.startMotionPriority(firstMotion, false, 1);
    } else {
      this._motionManager.updateMotion(this._model, dt);
    }
    this._model.saveParameters();
    this._model.update();

    const viewport = [0, 0, this.canvas.width, this.canvas.height];
    const matrix = new CubismMatrix44();
    matrix.loadIdentity();
    matrix.scale(1 / Math.max(this.canvas.width / this.canvas.height, 0.01), 1);
    matrix.multiplyByMatrix(this._modelMatrix);
    this.getRenderer().setRenderState(null, viewport);
    this.getRenderer().setMvpMatrix(matrix);
    this.getRenderer().drawModel(SHADER_PATH);
  }
}

class Live2DBridge {
  private canvas: HTMLCanvasElement | null = null;
  private gl: WebGLRenderingContext | WebGL2RenderingContext | null = null;
  private models = new Map<number, RuntimeModel>();
  private raf = 0;

  loadModelFromBytes(id: number, storage: string, bytes: Uint8Array): void {
    void this.loadModelFromBytesAsync(id, storage, bytes);
  }

  setVisible(id: number, visible: boolean): void {
    const model = this.models.get(id);
    if (model) {
      model.visible = visible;
    }
    this.updateCanvasVisibility();
  }

  release(id: number): void {
    this.models.delete(id);
    this.updateCanvasVisibility();
  }

  progress(): void {
    this.startLoop();
  }

  private async loadModelFromBytesAsync(id: number, storage: string, bytes: Uint8Array): Promise<void> {
    if (!(await ensureFramework())) {
      return;
    }
    this.ensureGl();
    if (!this.canvas || !this.gl) {
      throw new Error("Live2D canvas/WebGL initialization failed");
    }

    const previous = this.models.get(id);
    if (previous) {
      this.models.delete(id);
    }

    const model = new RuntimeModel(this.canvas, this.gl, id, storage);
    this.models.set(id, model);
    this.canvas.style.display = "block";
    await model.load(bytes);
    this.startLoop();
  }

  private ensureGl(): void {
    const canvas = makeCanvas();
    this.canvas = canvas;
    this.gl =
      canvas.getContext("webgl2", { alpha: true, antialias: true }) ??
      canvas.getContext("webgl", { alpha: true, antialias: true });
    window.addEventListener("resize", () => {
      makeCanvas();
    });
  }

  private startLoop(): void {
    if (this.raf) {
      return;
    }
    const tick = () => {
      this.raf = requestAnimationFrame(tick);
      if (!this.canvas || !this.gl) {
        return;
      }
      makeCanvas();
      this.gl.viewport(0, 0, this.canvas.width, this.canvas.height);
      this.gl.clearColor(0, 0, 0, 0);
      this.gl.clear(this.gl.COLOR_BUFFER_BIT);
      for (const model of this.models.values()) {
        model.draw();
      }
      this.updateCanvasVisibility();
    };
    tick();
  }

  private updateCanvasVisibility(): void {
    if (!this.canvas) {
      return;
    }
    const anyVisible = [...this.models.values()].some((model) => model.visible);
    this.canvas.style.display = anyVisible ? "block" : "none";
  }
}

const bridge = new Live2DBridge();
window.AetherKiriLive2D = bridge;
for (const pending of window.__aetherKiriPendingLive2D ?? []) {
  bridge.loadModelFromBytes(pending.id, pending.storage, pending.bytes);
}
window.__aetherKiriPendingLive2D = [];
log("bridge installed");
