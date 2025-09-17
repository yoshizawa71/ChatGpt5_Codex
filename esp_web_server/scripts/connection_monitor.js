// connection_monitor.js (versão sem jQuery, com backoff e awareness de AP suspenso)
(() => {
  const PING_URL               = "/ping";
  const NORMAL_INTERVAL_MS     = 1500;     // ping normal
  const HIDDEN_INTERVAL_MS     = 5000;     // aba em segundo plano
  const SUSPENDED_INTERVAL_MS  = 4000;     // AP em janela de silêncio
  const FETCH_TIMEOUT_MS       = 2000;     // timeout de cada ping
  const MAX_BACKOFF_MS         = 30000;    // teto do backoff exponencial
  const RELOAD_AFTER_OFFLINE_MS= 15000;    // se ficou >15s offline, recarrega ao voltar

  let fails = 0;
  let online = true;
  let loopTimer = null;
  let lastOnlineTs = Date.now();
  let lastReconnectReloadTs = 0;

  function jitter(ms) {
    return ms + Math.floor(Math.random() * 250); // jitter leve
  }

  function nextInterval(state) {
    if (document.hidden) return HIDDEN_INTERVAL_MS;
    if (state.ap_suspended) return SUSPENDED_INTERVAL_MS;
    if (online && state.ok) return NORMAL_INTERVAL_MS;

    // Backoff exponencial quando offline
    const base = 1000 * Math.pow(2, Math.min(8, fails)); // 1s,2s,4s,... até 256s
    return Math.min(MAX_BACKOFF_MS, base);
  }

  function showBanner(isConnected, message) {
    let el = document.getElementById("connection-warning");
    if (!el) {
      el = document.createElement("div");
      el.id = "connection-warning";
      Object.assign(el.style, {
        position: "fixed", top: "0", left: "0", right: "0",
        backgroundColor: "#ffedd5", color: "#7c2d12", textAlign: "center",
        padding: "8px", zIndex: "9999", fontFamily: "system-ui, sans-serif"
      });
      document.body.appendChild(el);
    }
    if (isConnected) {
      el.style.display = "none";
    } else {
      el.textContent = "⚠️ Conexão interrompida. " + (message || "Tentando reconectar…");
      el.style.display = "block";
    }
  }

  function countdownBanner(sec) {
    showBanner(false, `AP temporariamente indisponível. Voltará em ~${sec}s…`);
  }

  function maybeReloadOnReconnect() {
    const now = Date.now();
    const wasOfflineMs = now - lastOnlineTs;
    // Evita loop de reload: só recarrega se ficou “bem offline” e não recarregou há <10s
    if (wasOfflineMs > RELOAD_AFTER_OFFLINE_MS && (now - lastReconnectReloadTs) > 10000) {
      lastReconnectReloadTs = now;
      location.reload();
    }
  }

  async function ping() {
    // usa AbortSignal.timeout (Chrome 115+, Safari 17+)
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);

    try {
      const res = await fetch(PING_URL, {
        cache: "no-store",
        keepalive: false,
        signal: controller.signal,
        headers: { "Accept": "application/json,text/plain;q=0.8,*/*;q=0.1" }
      });
      clearTimeout(timeout);

      if (!res.ok) throw new Error("bad status " + res.status);

      let state = { ok: true, ap_running: true, ap_suspended: false, resume_in: 0, sta_count: undefined };

      const ctype = res.headers.get("content-type") || "";
      if (ctype.includes("application/json")) {
        // Backend “novo”: usa campos extras se vierem
        const j = await res.json().catch(() => ({}));
        state.ok           = !!j.ok || res.ok;
        if (typeof j.ap_running   === "boolean") state.ap_running   = j.ap_running;
        if (typeof j.ap_suspended === "boolean") state.ap_suspended = j.ap_suspended;
        if (typeof j.resume_in    === "number")  state.resume_in    = j.resume_in;
        if (typeof j.sta_count    === "number")  state.sta_count    = j.sta_count;
      } else {
        // Backend “legado”: expecta texto "OK"
        const t = await res.text();
        state.ok = (t || "").trim().toUpperCase().startsWith("OK");
      }

      // Sucesso
      fails = 0;
      if (!online) {
        online = true;
        showBanner(true);
        maybeReloadOnReconnect();  // recarrega só se ficou tempo demais off
      }
      lastOnlineTs = Date.now();

      // Se AP está suspenso, mostre contagem
      if (state.ap_suspended && state.resume_in > 0) {
        countdownBanner(state.resume_in);
      }

      // Próxima iteração baseada no estado
      scheduleNext(state);
    } catch (err) {
      clearTimeout(timeout);
      fails++;
      online = false;

      if (navigator && navigator.onLine === false) {
        showBanner(false, "Sem internet no dispositivo. Verifique Wi-Fi/dados.");
      } else {
        showBanner(false, "Reconectando…");
      }

      scheduleNext({ ok: false, ap_suspended: false });
    }
  }

  function scheduleNext(state) {
    if (loopTimer) clearTimeout(loopTimer);
    const wait = jitter(nextInterval(state));
    loopTimer = setTimeout(loop, wait);
  }

  function loop() {
    // Se o browser está offline, não adianta bater no ESP agora
    if (navigator && navigator.onLine === false) {
      online = false;
      showBanner(false, "Sem internet no dispositivo.");
      // aguarda um pouco e tenta depois
      scheduleNext({ ok: false, ap_suspended: false });
      return;
    }
    ping();
  }

  // Eventos do browser para acelerar reconexão
  window.addEventListener("online",  () => { fails = 0; loop(); });
  window.addEventListener("visibilitychange", () => {
    // Ao voltar a aba, tenta rápido
    if (!document.hidden) { fails = 0; loop(); }
  });

  // Start
  loop();
})();
