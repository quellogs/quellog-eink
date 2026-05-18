import { resolve } from "node:path";
import { defineConfig } from "vite";

const deviceOrigin = process.env.QUELLOG_WIFI_TARGET?.trim();

function quellogMockApi() {
  const mockNetworks = [
    { ssid: "Quellog Studio", rssi: -41, authmode: 3 },
    { ssid: "Cafe Reading Room", rssi: -58, authmode: 3 },
    { ssid: "Guest Network", rssi: -72, authmode: 0 }
  ];
  const savedCredentials = [
    { ssid: "Quellog Studio", password: "studio-pass", ipMode: "dhcp" },
    {
      ssid: "Guest Network",
      password: "",
      ipMode: "static",
      ip: "192.168.1.60",
      netmask: "255.255.255.0",
      gateway: "192.168.1.1",
      dns1: "223.5.5.5",
      dns2: "119.29.29.29"
    }
  ];

  return {
    name: "quellog-mock-api",
    configureServer(server) {
      if (deviceOrigin) {
        return;
      }

      server.middlewares.use((req, res, next) => {
        const url = req.url?.split("?")[0];
        if (req.method === "GET" && url === "/scan") {
          res.setHeader("Content-Type", "application/json");
          res.end(JSON.stringify({ aps: mockNetworks }));
          return;
        }

        if (req.method === "GET" && url === "/credentials") {
          res.setHeader("Content-Type", "application/json");
          res.end(JSON.stringify({
            credentials: savedCredentials.map((item) => ({
              ssid: item.ssid,
              isOpen: !item.password,
              ipMode: item.ipMode || "dhcp"
            }))
          }));
          return;
        }

        if (req.method === "GET" && url === "/setup-context") {
          res.setHeader("Content-Type", "application/json");
          res.end(JSON.stringify({ ssid: "Cafe Reading Room" }));
          return;
        }

        if (req.method === "POST" && url === "/submit") {
          let body = "";
          req.on("data", (chunk) => {
            body += chunk.toString();
          });
          req.on("end", () => {
            try {
              const payload = JSON.parse(body || "{}");
              const ssid = String(payload.ssid || "").trim();
              if (!ssid) {
                res.statusCode = 400;
                res.setHeader("Content-Type", "application/json");
                res.end(JSON.stringify({ success: false, error: "请填写 Wi‑Fi 名称。" }));
                return;
              }
              const password = String(payload.password || "");
              const ipMode = payload.ipMode === "static" ? "static" : "dhcp";
              const existingIndex = savedCredentials.findIndex((item) => item.ssid === ssid);
              if (existingIndex >= 0) {
                savedCredentials.splice(existingIndex, 1);
              }
              savedCredentials.unshift({
                ssid,
                password,
                ipMode,
                ip: String(payload.ip || ""),
                netmask: String(payload.netmask || ""),
                gateway: String(payload.gateway || ""),
                dns1: String(payload.dns1 || ""),
                dns2: String(payload.dns2 || "")
              });
              if (savedCredentials.length > 5) {
                savedCredentials.length = 5;
              }
              res.setHeader("Content-Type", "application/json");
              res.end(JSON.stringify({ success: true }));
            } catch {
              res.statusCode = 400;
              res.setHeader("Content-Type", "application/json");
              res.end(JSON.stringify({ success: false, error: "JSON 无效" }));
            }
          });
          return;
        }

        if (req.method === "POST" && url === "/credentials/delete") {
          let body = "";
          req.on("data", (chunk) => {
            body += chunk.toString();
          });
          req.on("end", () => {
            try {
              const payload = JSON.parse(body || "{}");
              const ssid = String(payload.ssid || "").trim();
              if (!ssid) {
                res.statusCode = 400;
                res.setHeader("Content-Type", "application/json");
                res.end(JSON.stringify({ success: false, error: "缺少 Wi‑Fi 名称。" }));
                return;
              }
              const existingIndex = savedCredentials.findIndex((item) => item.ssid === ssid);
              if (existingIndex >= 0) {
                savedCredentials.splice(existingIndex, 1);
              }
              res.setHeader("Content-Type", "application/json");
              res.end(JSON.stringify({ success: true }));
            } catch {
              res.statusCode = 400;
              res.setHeader("Content-Type", "application/json");
              res.end(JSON.stringify({ success: false, error: "JSON 无效" }));
            }
          });
          return;
        }

        if (req.method === "POST" && url === "/exit") {
          res.setHeader("Content-Type", "application/json");
          res.end(JSON.stringify({ success: true }));
          return;
        }

        next();
      });
    }
  };
}

export default defineConfig({
  plugins: [quellogMockApi()],
  base: "./",
  server: deviceOrigin
    ? {
        proxy: {
          "/scan": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/submit": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/credentials": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/setup-context": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/credentials/delete": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/exit": {
            target: deviceOrigin,
            changeOrigin: true
          },
          "/done.html": {
            target: deviceOrigin,
            changeOrigin: true
          }
        }
      }
    : undefined,
  preview: {
    port: 4173
  },
  resolve: {
    alias: {
      "@": resolve(__dirname, "src")
    }
  }
});
