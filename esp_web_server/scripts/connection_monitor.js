// connection_monitor.js: Monitoramento + Reconexão Automática com limite de tentativas

(function(){
  let failCount           = 0;
  let reconnectAttempts   = 0;
  const MAX_FAILS         = 2;     // 2 falhas seguidas → entra em reconexão
  const CHECK_INTERVAL    = 1000;  // 1 s entre pings normais
  const RECONNECT_INTERVAL= 2000;  // 2 s entre tentativas de reconexão
  const MAX_RECONNECT_ATTEMPTS = 5; // após 5 tentativas no modo reconectar, desiste

  let checkTimer     = null;
  let reconnectTimer = null;

  // Inicia o polling de /ping
  function startChecking() {
    if (checkTimer) return;
    checkTimer = setInterval(checkConnection, CHECK_INTERVAL);
  }

  // Para o polling normal
  function stopChecking() {
    clearInterval(checkTimer);
    checkTimer = null;
  }

  // Entra em modo “reconectar”
  function startReconnecting() {
    if (reconnectTimer) return;
    reconnectAttempts = 0;
    reconnectTimer = setInterval(() => {
      $.ajax({ url: '/ping', timeout: 1000 })
        .done(() => {
          stopReconnecting();
          showConnectionStatus(true);
          location.reload(); // recarrega o app assim que reconecta
        })
        .fail(() => {
          reconnectAttempts++;
          if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            stopReconnecting();
            showPermanentFailure();
          }
        });
    }, RECONNECT_INTERVAL);
  }

  // Para o modo reconectar
  function stopReconnecting() {
    clearInterval(reconnectTimer);
    reconnectTimer = null;
  }

  // Checagem única de /ping
  function checkConnection() {
    $.ajax({ url: '/ping', timeout: 1000 })
      .done(() => {
        failCount = 0;
        showConnectionStatus(true);
      })
      .fail(() => {
        failCount++;
        if (failCount >= MAX_FAILS) {
          stopChecking();
          startReconnecting();
          showConnectionStatus(false, "Reconectando…");
        }
      });
  }

  // Exibe aviso genérico ou de fallback
  function showConnectionStatus(isConnected, message) {
    let el = document.getElementById("connection-warning");
    if (!el) {
      el = document.createElement("div");
      el.id = "connection-warning";
      Object.assign(el.style, {
        position:       "fixed",
        top:            "0",
        left:           "0",
        right:          "0",
        backgroundColor: "#ffcccc",
        color:           "#900",
        textAlign:       "center",
        padding:         "8px",
        zIndex:          "9999"
      });
      document.body.appendChild(el);
    }
    if (isConnected) {
      el.style.display = "none";
    } else {
      el.textContent   = "⚠️ Conexão interrompida. " + (message||"Tentando reconectar…");
      el.style.display = "block";
    }
  }

  // Exibe mensagem definitiva após esgotar tentativas
  function showPermanentFailure() {
    showConnectionStatus(false, "Não foi possível reconectar. Por favor, verifique o Wi-Fi.");
  }

  // Inicia tudo ao carregar a página
  window.addEventListener("DOMContentLoaded", startChecking);
})();
