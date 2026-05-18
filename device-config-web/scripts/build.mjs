import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { build } from "vite";

const rootDir = resolve(import.meta.dirname, "..");
const distDir = resolve(rootDir, "dist");
const tempRootDir = resolve(rootDir, ".vite-build");

function inlineBuiltHtml(outDir, htmlFileName) {
  const htmlPath = resolve(outDir, htmlFileName);
  let html = readFileSync(htmlPath, "utf-8");
  const inlineScripts = [];

  html = html.replace(/<script[^>]*src="([^"]+)"[^>]*><\/script>/g, (_, src) => {
    const scriptPath = resolve(outDir, src);
    const code = readFileSync(scriptPath, "utf-8");
    inlineScripts.push(`<script>${code}</script>`);
    return "";
  });

  html = html.replace(/<link[^>]*href="([^"]+)"[^>]*>/g, (fullMatch, href) => {
    if (!href.endsWith(".css")) {
      return "";
    }
    const stylePath = resolve(outDir, href);
    const css = readFileSync(stylePath, "utf-8");
    return `<style>${css}</style>`;
  });

  if (inlineScripts.length > 0) {
    html = html.replace("</body>", `${inlineScripts.join("\n")}\n  </body>`);
  }

  return html;
}

async function buildPage(name, htmlEntry) {
  const outDir = resolve(tempRootDir, name);
  await build({
    configFile: false,
    root: rootDir,
    publicDir: false,
    base: "./",
    build: {
      outDir,
      emptyOutDir: true,
      cssCodeSplit: false,
      rollupOptions: {
        input: resolve(rootDir, htmlEntry)
      }
    }
  });

  mkdirSync(dirname(resolve(distDir, `${name}.html`)), { recursive: true });
  writeFileSync(resolve(distDir, `${name}.html`), `${inlineBuiltHtml(outDir, `${name}.html`)}\n`);
}

async function main() {
  rmSync(distDir, { recursive: true, force: true });
  rmSync(tempRootDir, { recursive: true, force: true });
  mkdirSync(distDir, { recursive: true });

  await buildPage("index", "index.html");
  await buildPage("done", "done.html");
  await buildPage("settings", "settings.html");

  const versionNote = [
    "Built by device-config-web/scripts/build.mjs",
    "This directory is a generated firmware embedding input."
  ].join("\n");
  writeFileSync(resolve(distDir, ".buildinfo"), `${versionNote}\n`);
  rmSync(tempRootDir, { recursive: true, force: true });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
