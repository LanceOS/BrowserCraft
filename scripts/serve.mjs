import { createServer } from "node:http";
import { createReadStream, existsSync, statSync } from "node:fs";
import { extname, join, normalize } from "node:path";
import { cwd } from "node:process";

const root = cwd();
const port = Number(process.env.PORT ?? 4173);

const mimeTypes = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".txt": "text/plain; charset=utf-8",
};

const resolvePath = (urlPath) => {
  const cleanPath = urlPath === "/" ? "/index.html" : urlPath;
  const resolved = normalize(join(root, cleanPath));
  if (!resolved.startsWith(root)) return null;
  if (!existsSync(resolved)) return null;
  if (statSync(resolved).isDirectory()) return join(resolved, "index.html");
  return resolved;
};

createServer((req, res) => {
  const requestPath = req.url?.split("?")[0] ?? "/";
  const filePath = resolvePath(requestPath);

  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");

  if (!filePath || !existsSync(filePath)) {
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("Not found");
    return;
  }

  const ext = extname(filePath);
  res.writeHead(200, {
    "Content-Type": mimeTypes[ext] ?? "application/octet-stream",
    "Cache-Control": "no-store",
  });
  createReadStream(filePath).pipe(res);
}).listen(port, () => {
  console.log(`Serving voxel sandbox at http://localhost:${port}`);
});
