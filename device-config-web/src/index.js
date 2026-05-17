import "./styles.css";
import { loadSetupContext, submitCredentials } from "./api.js";

const ssidInput = document.querySelector("#ssid");
const passwordInput = document.querySelector("#password");
const submitButton = document.querySelector("#submit");
const togglePasswordButton = document.querySelector("#toggle-password");
const statusEl = document.querySelector("#status");
const selectedNetworkEl = document.querySelector("#selected-network");
const selectedSsidEl = document.querySelector("#selected-ssid");
const selectedSourceEl = document.querySelector("#selected-source");

const state = {
  activeSsid: "",
  ssidSource: "loading"
};

function setStatus(text, tone = "neutral") {
  statusEl.textContent = text;
  statusEl.dataset.tone = tone;
}

function setBusyState(isBusy) {
  submitButton.disabled = isBusy;
  ssidInput.disabled = isBusy;
  passwordInput.disabled = isBusy;
  togglePasswordButton.disabled = isBusy;
}

function getSetupContextSsid(data) {
  const candidates = [data?.ssid, data?.pendingSsid, data?.pending_ssid];
  const ssid = candidates.find((value) => typeof value === "string" && value.trim());
  return typeof ssid === "string" ? ssid.trim() : "";
}

function updateSelectedNetwork() {
  selectedNetworkEl.dataset.state = state.ssidSource;
  selectedSsidEl.textContent = state.activeSsid || (state.ssidSource === "loading" ? "读取中" : "未选择");

  const sourceText = {
    device: "使用墨水屏选择的网络",
    manual: "使用手动输入的网络",
    loading: "正在读取设备上的目标网络",
    empty: "请在墨水屏选择网络，或手动输入"
  };
  selectedSourceEl.textContent = sourceText[state.ssidSource] || sourceText.empty;
}

function setActiveSsid(ssid, source, { syncInput = true } = {}) {
  state.activeSsid = ssid.trim();
  state.ssidSource = state.activeSsid ? source : "empty";
  if (syncInput) {
    ssidInput.value = state.activeSsid;
  }
  updateSelectedNetwork();
}

async function applySetupContext() {
  try {
    const data = await loadSetupContext();
    const ssid = getSetupContextSsid(data);
    if (ssid) {
      setActiveSsid(ssid, "device");
      setStatus("输入密码后连接。");
      passwordInput.focus();
      return;
    }
  } catch {
    // Keep the page usable when the setup context request is interrupted by the phone OS.
  }

  setActiveSsid(ssidInput.value, ssidInput.value.trim() ? "manual" : "empty", { syncInput: false });
  setStatus("请确认 Wi‑Fi 名称并输入密码。");
}

async function handleSubmit() {
  const ssid = (state.activeSsid || ssidInput.value).trim();
  const password = passwordInput.value;

  if (!ssid) {
    setStatus("请填写 Wi‑Fi 名称。", "error");
    ssidInput.focus();
    return;
  }

  setBusyState(true);
  setStatus("正在提交，设备将关闭热点并连接 Wi‑Fi。");

  try {
    const result = await submitCredentials(ssid, password);
    if (!result.success) {
      setBusyState(false);
      setStatus(result.error || "提交失败，请检查后重试。", "error");
      return;
    }

    setStatus("已提交，设备正在切换网络。", "success");
    window.setTimeout(() => {
      window.location.href = "/done.html";
    }, 500);
  } catch (error) {
    setBusyState(false);
    setStatus(error instanceof Error ? error.message : "提交失败，请稍后重试。", "error");
  }
}

ssidInput.addEventListener("input", () => {
  setActiveSsid(ssidInput.value, "manual", { syncInput: false });
});

togglePasswordButton.addEventListener("click", () => {
  const shouldShow = passwordInput.type === "password";
  passwordInput.type = shouldShow ? "text" : "password";
  togglePasswordButton.textContent = shouldShow ? "隐藏" : "显示";
});

submitButton.addEventListener("click", () => {
  void handleSubmit();
});

passwordInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    void handleSubmit();
  }
});

updateSelectedNetwork();
void applySetupContext();
