import "./styles.css";
import { loadDeviceApiSettings, saveDeviceApiSettings } from "./api.js";

const baseUrlInput = document.querySelector("#base-url");
const usernameInput = document.querySelector("#username");
const passwordInput = document.querySelector("#password");
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
  usernameInput.disabled = isBusy;
  passwordInput.disabled = isBusy;
}

function updateHint(isPasswordConfigured) {
  hintEl.textContent = isPasswordConfigured
    ? "已保存密码，留空则保持不变。"
    : "尚未保存密码。";
}

async function loadSettings() {
  try {
    const settings = await loadDeviceApiSettings();
    baseUrlInput.value = typeof settings.base_url === "string" ? settings.base_url : "";
    usernameInput.value = typeof settings.username === "string" ? settings.username : "";
    updateHint(Boolean(settings.password_configured));
    setStatus("");
  } catch (error) {
    hintEl.textContent = "配置读取失败。";
    setStatus(error instanceof Error ? error.message : "配置读取失败。", "error");
  }
}

async function handleSave() {
  const baseUrl = baseUrlInput.value.trim();
  const username = usernameInput.value.trim();
  const password = passwordInput.value;

  if (!baseUrl) {
    setStatus("请填写服务地址。", "error");
    baseUrlInput.focus();
    return;
  }
  if (!username) {
    setStatus("请填写用户名。", "error");
    usernameInput.focus();
    return;
  }

  setBusy(true);
  setStatus("正在保存...");

  try {
    const result = await saveDeviceApiSettings(baseUrl, username, password);
    if (!result.success) {
      throw new Error(result.error || "保存失败");
    }
    passwordInput.value = "";
    usernameInput.value = typeof result.username === "string" ? result.username : username;
    updateHint(Boolean(result.password_configured));
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
