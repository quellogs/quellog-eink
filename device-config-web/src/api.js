async function parseJsonResponse(response) {
  let body = {};
  try {
    body = await response.json();
  } catch {
    body = {};
  }

  if (!response.ok) {
    const message = typeof body.error === "string" && body.error ? body.error : "请求失败，请稍后重试。";
    throw new Error(message);
  }

  return body;
}

export async function scanNetworks() {
  const response = await fetch("/scan", {
    cache: "no-store",
    headers: {
      Accept: "application/json"
    }
  });
  return parseJsonResponse(response);
}

export async function loadCredentials() {
  const response = await fetch("/credentials", {
    cache: "no-store",
    headers: {
      Accept: "application/json"
    }
  });
  return parseJsonResponse(response);
}

export async function loadSetupContext() {
  const response = await fetch("/setup-context", {
    cache: "no-store",
    headers: {
      Accept: "application/json"
    }
  });
  return parseJsonResponse(response);
}

export async function submitCredentials(ssid, password) {
  const response = await fetch("/submit", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Accept: "application/json"
    },
    body: JSON.stringify({ ssid, password })
  });
  return parseJsonResponse(response);
}

export async function deleteCredential(ssid) {
  const response = await fetch("/credentials/delete", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Accept: "application/json"
    },
    body: JSON.stringify({ ssid })
  });
  return parseJsonResponse(response);
}

export async function exitConfiguration() {
  const response = await fetch("/exit", {
    method: "POST",
    headers: {
      Accept: "application/json"
    }
  });
  return parseJsonResponse(response);
}
