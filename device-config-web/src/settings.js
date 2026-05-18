import "./styles.css";
import { loadDeviceApiSettings, saveDeviceApiSettings } from "./api.js";

const baseUrlInput = document.querySelector("#base-url");
const apiTokenInput = document.querySelector("#api-token");
const hintEl = document.querySelector("#hint");
const saveButton = document.querySelector("#save");
const statusEl = document.querySelector("#status");

function setStatus(text, tone = "neutral") {
  statusEl.textContent = text;
  statusEl.dataset.tone = tone;
}

function setBusy(isBusy) {
  saveButton.disabled = isBusy;
  baseUrlInput.disabled = isBusy;
  apiTokenInput.disabled = isBusy;
}

function updateHint(isTokenConfigured) {
  hintEl.textContent = isTokenConfigured
    ? "已保存 Token，留空则保持不变。"
    : "尚未保存 Token。";
}

async function loadSettings() {
  try {
    const settings = await loadDeviceApiSettings();
    baseUrlInput.value = typeof settings.base_url === "string" ? settings.base_url : "";
    updateHint(Boolean(settings.api_token_configured));
    setStatus("");
  } catch (error) {
    hintEl.textContent = "配置读取失败。";
    setStatus(error instanceof Error ? error.message : "配置读取失败。", "error");
  }
}

async function handleSave() {
  const baseUrl = baseUrlInput.value.trim();
  const apiToken = apiTokenInput.value;

  if (!baseUrl) {
    setStatus("请填写服务地址。", "error");
    baseUrlInput.focus();
    return;
  }

  setBusy(true);
  setStatus("正在保存...");

  try {
    const result = await saveDeviceApiSettings(baseUrl, apiToken);
    if (!result.success) {
      throw new Error(result.error || "保存失败");
    }
    apiTokenInput.value = "";
    updateHint(Boolean(result.api_token_configured));
    setStatus("已保存。", "success");
  } catch (error) {
    setStatus(error instanceof Error ? error.message : "保存失败", "error");
  } finally {
    setBusy(false);
  }
}

saveButton.addEventListener("click", () => {
  void handleSave();
});

void loadSettings();
