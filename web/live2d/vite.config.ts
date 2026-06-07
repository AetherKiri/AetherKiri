import { resolve } from "node:path";
import { defineConfig } from "vite";

const live2dCoreStubBanner = `
(function () {
  if (typeof globalThis.Live2DCubismCore !== "undefined") return;
  var stub = null;
  var noop = function () { return 0; };
  stub = new Proxy(noop, {
    apply: function () { return 0; },
    construct: function () { return {}; },
    get: function (_target, prop) {
      if (prop === "then") return undefined;
      if (prop === "__aetherKiriCoreStub") return true;
      return stub;
    }
  });
  globalThis.Live2DCubismCore = stub;
  globalThis.__aetherKiriLive2DCoreStub = true;
})();
`;

export default defineConfig({
  build: {
    emptyOutDir: true,
    lib: {
      entry: resolve(__dirname, "aetherkiri-live2d-bridge.ts"),
      formats: ["iife"],
      name: "AetherKiriLive2DBridge",
      fileName: () => "aetherkiri-live2d-bridge.js"
    },
    outDir: resolve(__dirname, "../../out/live2d-bridge"),
    rollupOptions: {
      output: {
        banner: live2dCoreStubBanner,
        extend: true
      }
    },
    target: "es2022"
  }
});
