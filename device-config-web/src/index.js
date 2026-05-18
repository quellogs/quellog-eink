import "./styles.css";
import { loadCredentials, loadSetupContext, submitCredentials } from "./api.js";

const ssidInput = document.querySelector("#ssid");
const passwordInput = document.querySelector("#password");
const ipModeInputs = Array.from(document.querySelectorAll('input[name="ip-mode"]'));
const staticSettingsEl = document.querySelector("#static-settings");
const ipInput = document.querySelector("#ip");
const netmaskInput = document.querySelector("#netmask");
const gatewayInput = document.querySelector("#gateway");
const dns1Input = document.querySelector("#dns1");
const dns2Input = document.querySelector("#dns2");
const submitButton = document.querySelector("#submit");
const togglePasswordButton = document.querySelector("#toggle-password");
const statusEl = document.querySelector("#status");
const credentialsListEl = document.querySelector("#credentials");

const state = {
  activeSsid: "",
  ipMode: "dhcp"
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
  ipModeInputs.forEach((input) => {
    input.disabled = isBusy;
  });
  [ipInput, netmaskInput, gatewayInput, dns1Input, dns2Input].forEach((input) => {
    input.disabled = isBusy;
  });
}

function getSetupContextSsid(data) {
  const candidates = [data?.ssid, data?.pendingSsid, data?.pending_ssid];
  const ssid = candidates.find((value) => typeof value === "string" && value.trim());
  return typeof ssid === "string" ? ssid.trim() : "";
}

function setActiveSsid(ssid, { syncInput = true } = {}) {
  state.activeSsid = ssid.trim();
  if (syncInput) {
    ssidInput.value = state.activeSsid;
  }
}

async function applySetupContext() {
  try {
    const data = await loadSetupContext();
    const ssid = getSetupContextSsid(data);
    if (ssid) {
      setActiveSsid(ssid);
      setStatus("输入密码后连接。");
      passwordInput.focus();
      return;
    }
  } catch {
    // Keep the page usable when the setup context request is interrupted by the phone OS.
  }

  setActiveSsid(ssidInput.value, { syncInput: false });
  setStatus("请确认 Wi‑Fi 名称并输入密码。");
}

function isValidIpv4(value) {
  const parts = value.trim().split(".");
  return parts.length === 4 && parts.every((part) => {
    if (!/^\d{1,3}$/.test(part)) {
      return false;
    }
    const number = Number(part);
    return number >= 0 && number <= 255 && String(number) === part.replace(/^0+(?=\d)/, "");
  });
}

function getIpMode() {
  return ipModeInputs.find((input) => input.checked)?.value === "static" ? "static" : "dhcp";
}

function updateIpMode() {
  state.ipMode = getIpMode();
  staticSettingsEl.hidden = state.ipMode !== "static";
}

function buildSubmitPayload(ssid, password) {
  const payload = {
    ssid,
    password,
    ipMode: getIpMode()
  };

  if (payload.ipMode !== "static") {
    return payload;
  }

  payload.ip = ipInput.value.trim();
  payload.netmask = netmaskInput.value.trim();
  payload.gateway = gatewayInput.value.trim();
  payload.dns1 = dns1Input.value.trim();
  payload.dns2 = dns2Input.value.trim();

  const requiredFields = [
    [payload.ip, ipInput, "请填写有效的 IP 地址。"],
    [payload.netmask, netmaskInput, "请填写有效的子网掩码。"],
    [payload.gateway, gatewayInput, "请填写有效的网关。"],
    [payload.dns1, dns1Input, "请填写有效的主 DNS。"]
  ];

  for (const [value, input, message] of requiredFields) {
    if (!isValidIpv4(value)) {
      setStatus(message, "error");
      input.focus();
      return null;
    }
  }

  if (payload.dns2 && !isValidIpv4(payload.dns2)) {
    setStatus("请填写有效的备 DNS，或留空。", "error");
    dns2Input.focus();
    return null;
  }

  return payload;
}

function renderCredentials(credentials) {
  credentialsListEl.textContent = "";
  if (!Array.isArray(credentials) || credentials.length === 0) {
    const item = document.createElement("li");
    item.innerHTML = "<strong>暂无已保存网络</strong><span>DHCP</span>";
    credentialsListEl.append(item);
    return;
  }

  credentials.forEach((credential) => {
    const item = document.createElement("li");
    const ssid = document.createElement("strong");
    const meta = document.createElement("span");
    ssid.textContent = credential.ssid || "未命名网络";
    meta.textContent = credential.ipMode === "static" ? "手动 IP" : "DHCP";
    item.append(ssid, meta);
    credentialsListEl.append(item);
  });
}

async function applyCredentials() {
  try {
    const data = await loadCredentials();
    renderCredentials(data?.credentials);
  } catch {
    renderCredentials([]);
  }
}

async function handleSubmit() {
  const ssid = (state.activeSsid || ssidInput.value).trim();
  const password = passwordInput.value;

  if (!ssid) {
    setStatus("请填写 Wi‑Fi 名称。", "error");
    ssidInput.focus();
    return;
  }

  const payload = buildSubmitPayload(ssid, password);
  if (payload === null) {
    return;
  }

  setBusyState(true);
  setStatus("正在提交，设备将关闭热点并连接 Wi‑Fi。");

  try {
    const result = await submitCredentials(payload);
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
  setActiveSsid(ssidInput.value, { syncInput: false });
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

ipModeInputs.forEach((input) => {
  input.addEventListener("change", updateIpMode);
});

updateIpMode();
void applySetupContext();
void applyCredentials();
