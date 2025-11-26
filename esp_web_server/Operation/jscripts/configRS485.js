// configRS485.js
// UI RS-485: formulário para adicionar e lista de sensores com status

// ===== Config =====
const RS485_MAX_SENSORS = 10;
const CHANNEL_OPTIONS = [3,4,5,6,7,8,9,10,11,12]; // ajuste conforme seu hardware
// Endereços Modbus oferecidos no dropdown (ajuste o range se precisar)
const ADDRESS_OPTIONS = Array.from({ length: 10 }, (_, i) => i + 1); // 1..10
// ===== Estado =====
const sensorMap = []; // { channel, address, type, subtype }

// ===== Utils =====
function parseAddr(s) {
  if (typeof s !== 'string') return NaN;
  const t = s.trim();
  if (!t) return NaN;
  if (t.startsWith('0x') || t.startsWith('0X')) return parseInt(t, 16);
  return parseInt(t, 10);
}

function gatherSensorMap() {
  return sensorMap.slice().sort((a, b) => a.channel - b.channel);
}

function refreshChannelOptions() {
  const used = new Set(sensorMap.map(s => s.channel));
  const $sel = $('#channel_input');
  $sel.empty();
  $sel.append('<option value="">Selecione…</option>');
  CHANNEL_OPTIONS.forEach(ch => {
    const disabled = used.has(ch) ? 'disabled' : '';
    $sel.append(`<option value="${ch}" ${disabled}>${ch}</option>`);
  });
}

function refreshAddressOptions() {
  const usedAddrs = new Set(sensorMap.map(s => s.address));
  const $sel = $('#addr_input');
  if (!$sel.length) return;

  $sel.empty();
  $sel.append('<option value="">Selecione…</option>');

  ADDRESS_OPTIONS.forEach(addr => {
    const disabled = usedAddrs.has(addr) ? 'disabled' : '';
    $sel.append(`<option value="${addr}" ${disabled}>${addr}</option>`);
  });
}

async function deleteSensorBackend(ch, addr) {
  const res = await fetch(`/rs485ConfigDelete?channel=${encodeURIComponent(ch)}&address=${encodeURIComponent(addr)}`);
  const j = await res.json().catch(() => ({}));
  if (!res.ok || !j || j.ok !== true) {
    throw new Error((j && (j.error || j.msg)) || 'Falha ao excluir no backend.');
  }
  return j;
}

function rs485RenderList(items) {
  const $list = $('#rs485-list');
  if (!$list.length) return;

  $list.empty();

  const sorted = (items || []).slice().sort((a, b) => a.channel - b.channel);
  if (!sorted.length) {
    $list.append('<div class="hint">Nenhum sensor cadastrado</div>');
    return;
  }

  sorted.forEach((s) => {
    const desc = `Canal ${s.channel} – Endereço ${s.address} – ${s.type || ''}${s.subtype ? ' (' + s.subtype + ')' : ''}`;

    const $line = $('<div class="sensor-line">');
    const $desc = $('<span class="sensor-desc">').text(desc);
    const $icon = $('<span class="status-icon led circle disconnected" aria-label="status"></span>');
    const $remove = $('<button type="button" class="rm-btn">Remover</button>').on('click', async () => {
      if (!confirm(`Remover o sensor do Canal ${s.channel}, Endereço ${s.address}?`)) return;
      $remove.prop('disabled', true).text('Removendo...');
      try {
        await deleteSensorBackend(s.channel, s.address);
        const i = sensorMap.findIndex(x => x.channel === s.channel && x.address === s.address);
        if (i >= 0) sensorMap.splice(i, 1);
        refreshChannelOptions();
        refreshAddressOptions();
        rs485RenderList(sensorMap);
        rs485FetchAndRender();
        
      } catch (e) {
        alert(e && e.message ? e.message : 'Falha ao remover.');
        $remove.prop('disabled', false).text('Remover');
      }
    });

    $line.append($desc, $('<span>').append($icon, ' ', $remove));
    $list.append($line);

    $.getJSON(`/rs485Ping?channel=${s.channel}&address=${s.address}&ts=${Date.now()}`)
      .done(res => {
        $icon.toggleClass('connected', !!(res && res.alive))
             .toggleClass('disconnected', !(res && res.alive));
      })
      .fail(() => {
        $icon.removeClass('connected').addClass('disconnected');
      });
  });
}

function rs485SyncLocalMap(list) {
  sensorMap.length = 0;

  (list || []).forEach((s) => {
    if (!s) return;
    const ch = Number(s.channel);
    const addr = Number(s.address);
    if (!Number.isInteger(ch) || ch <= 0) return;
    if (!Number.isInteger(addr) || addr < 1 || addr > 247) return;
    if (sensorMap.some(x => x.channel === ch) || sensorMap.some(x => x.address === addr)) return;

    sensorMap.push({
      channel: ch,
      address: addr,
      type: (s.type || '').toString(),
      subtype: (s.subtype || '').toString()
    });
  });

  refreshChannelOptions();
  refreshAddressOptions();
  rs485RenderList(sensorMap);

}

function rs485FetchAndRender() {
  return $.getJSON('/rs485ConfigGet')
    .done(data => {
      const items = (data && Array.isArray(data.sensors)) ? data.sensors : [];
      rs485SyncLocalMap(items);
    })
    .fail(() => {
      rs485RenderList(sensorMap);
    });
}

function updateSensorStatusList() {
  rs485RenderList(sensorMap);
}

// ====== Formulário: adicionar sensor ======
async function addSensorFromInputs() {
  if (sensorMap.length >= RS485_MAX_SENSORS) {
    alert(`Limite de ${RS485_MAX_SENSORS} sensores atingido.`);
    return;
  }

  const ch      = parseInt($('#channel_input').val(), 10);
  const addr    = parseAddr($('#addr_input').val());
  const type    = $('#type_input').val();
  const subtype = $('#subtype_input').val();

  if (!ch) {
    alert('Selecione o canal.');
    return;
  }
  if (!Number.isInteger(addr) || addr < 1 || addr > 247) {
    alert('Endereço inválido (1..247).');
    return;
  }
  if (!type) {
    alert('Selecione o tipo.');
    return;
  }

  if (type !== 'energia') {
    $('#subtype_input').val('');
  }

  if (sensorMap.some(s => s.channel === ch)) {
    alert('Canal já utilizado.');
    return;
  }
  if (sensorMap.some(s => s.address === addr)) {
    alert('Endereço já utilizado.');
    return;
  }

  const isEnergy = (type === 'energia');

  // Regras de ping:
  // - Para tipos normais: exige LED verde (ping estável).
  // - Para energia (JSY): permite continuar mesmo sem verde, com confirmação.
  if (!isTopPingStable()) {
    if (!isEnergy) {
      alert('Aguardando detecção estável do sensor (indicador verde) antes de adicionar.');
      return;
    }

    const proceed = confirm(
      'O sensor ainda não respondeu nesse endereço (indicador amarelo/vermelho).\n' +
      'Se for um medidor de energia JSY novo, o sistema pode tentar encontrar o endereço ' +
      'de fábrica e ajustar automaticamente o endereço Modbus do equipamento para o valor escolhido.\n\n' +
      'Deseja continuar mesmo assim?'
    );
    if (!proceed) return;
  }

  // Chamada ao backend para registrar/persistir
  let ok = false;
  let errMsg = '';

  try {
    const res = await fetch('/rs485ConfigSave', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ sensors: [{ channel: ch, address: addr, type, subtype }] })
    });

    const j = await res.json().catch(() => ({}));
    ok = !!(res.ok && j && j.ok === true);
    errMsg = (j && j.error) ? String(j.error) : '';
  } catch (e) {
    errMsg = 'Falha de comunicação com o backend (/rs485ConfigSave).';
  }

  if (!ok) {
    alert(errMsg || 'Não foi possível registrar o dispositivo.');
    return;
  }

  // Sucesso: atualiza estado local
  sensorMap.push({ channel: ch, address: addr, type, subtype });
  refreshChannelOptions();
  refreshAddressOptions();
  updateSensorStatusList();

  // Re-sincroniza com o back-end
  rs485FetchAndRender();

  // Limpa formulário
  $('#channel_input').val('');
  $('#addr_input').val('');
  $('#type_input').val('');
  $('#subtype_input').val('').prop('disabled', true);

  // Reinicia estado de ping do topo
  resetTopPingState();
}


// ====== NOVO: Monitor de ping com histérese no formulário ======
const PING_INTERVAL_MS = 1500;   // menos carga
const HIST_SIZE        = 5;
const OK_FOR_FOUND     = 3;      // >=3 ok na janela => encontrado
const FAIL_FOR_MISSING = 3;      // >=3 falhas => ausente

let topPingHist = [];
let topPingTimer = null;
let topPingInFlight = false;     // evita concorrência

function setTopLedState(cls, text) {
  const $led = $('#pingLed');
  if ($led.length) {
    $led.removeClass('led--off led--yellow led--green led--red led-pulse').addClass(cls);
    if (cls === 'led--green') $led.addClass('led-pulse'); // pulsa até salvar
  }
  if ($('#pingStatus').length) $('#pingStatus').text(text || '');
}

function setAddButton(enabled, textWhenEnabled, textWhenDisabled) {
  const $b = $('#addSensor');
  if (!$b.length) return;
  $b.prop('disabled', !enabled);
  if (enabled && textWhenEnabled)   $b.text(textWhenEnabled);
  if (!enabled && textWhenDisabled) $b.text(textWhenDisabled);
}

function isInputsValidForPing() {
  const ch = parseInt($('#channel_input').val(), 10);
  const addr = parseAddr($('#addr_input').val());
  return Number.isInteger(ch) && ch > 0 && Number.isInteger(addr) && addr >= 1 && addr <= 247;
}

function isTopPingStable() {
  const ok = topPingHist.filter(Boolean).length;
  return ok >= OK_FOR_FOUND;
}

function resetTopPingState() {
  topPingHist = [];
  setTopLedState('led--off', '');

  const type = $('#type_input').val();
  const ch   = parseInt($('#channel_input').val(), 10);
  const addr = parseAddr($('#addr_input').val());
  const inputsOk =
    Number.isInteger(ch) && ch > 0 &&
    Number.isInteger(addr) && addr >= 1 && addr <= 247 &&
    !!type;

  const isEnergy = (type === 'energia');

  if (isEnergy && inputsOk) {
    // Para energia, não bloqueamos o botão pelo ping
    setAddButton(true, 'Adicionar sensor', '');
  } else {
    setAddButton(false, '', 'Aguardando sensor…');
  }
}

function updateTopUiByHistory() {
  const ok   = topPingHist.filter(Boolean).length;
  const fail = topPingHist.length - ok;

  if (!isInputsValidForPing()) {
    resetTopPingState();
    return;
  }

  const currentType = $('#type_input').val();
  const isEnergy    = (currentType === 'energia');

  if (ok >= OK_FOR_FOUND) {
    // Ping respondeu várias vezes nesse endereço -> sensor encontrado
    setTopLedState('led--green', 'Sensor encontrado (não salvo)');

    // Para todos os tipos, com ping verde deixamos adicionar
    setAddButton(true, 'Adicionar sensor', '');
  } else if (fail >= FAIL_FOR_MISSING) {
    if (isEnergy) {
      // JSY: pode ser dispositivo novo em outro endereço de fábrica
      setTopLedState(
        'led--yellow',
        'Sem resposta nesse endereço (pode ser JSY novo com outro endereço).'
      );
      // IMPORTANTE: para energia, NÃO bloqueamos o botão
      setAddButton(true, 'Adicionar sensor', '');
    } else {
      // Demais tipos: mantém rígido -> sem resposta, não deixa adicionar
      setTopLedState('led--red', 'Sem resposta');
      setAddButton(false, '', 'Aguardando sensor…');
    }
  } else if (topPingHist.length > 0) {
    // Janela pequena de histórico: ainda em fase de detecção
    setTopLedState('led--yellow', 'Procurando…');

    if (isEnergy) {
      // Para energia, mantemos o botão liberado quando os inputs são válidos
      const ch   = parseInt($('#channel_input').val(), 10);
      const addr = parseAddr($('#addr_input').val());
      const inputsOk =
        Number.isInteger(ch) && ch > 0 &&
        Number.isInteger(addr) && addr >= 1 && addr <= 247 &&
        !!currentType;
      setAddButton(!!inputsOk, 'Adicionar sensor', 'Aguardando sensor…');
    } else {
      setAddButton(false, '', 'Aguardando sensor…');
    }
  } else {
    // Nenhum ping ainda
    resetTopPingState();
  }
}


function pushTopResult(ok) {
  topPingHist.push(!!ok);
  if (topPingHist.length > HIST_SIZE) topPingHist.shift();
  updateTopUiByHistory();
}

async function topPingOnce() {
  if (topPingInFlight) return;             // evita sobreposição
  if (!isInputsValidForPing()) { resetTopPingState(); return; }
  topPingInFlight = true;

  const ch   = parseInt($('#channel_input').val(), 10);
  const addr = parseAddr($('#addr_input').val());

  try {
    const r = await $.getJSON(`/rs485Ping?channel=${ch}&address=${addr}&ts=${Date.now()}`);
    pushTopResult(r && r.alive === true);
  } catch {
    pushTopResult(false);
  } finally {
    topPingInFlight = false;
  }
}

function startTopPingMonitor() {
  if (topPingTimer) clearInterval(topPingTimer);
  resetTopPingState();
  topPingTimer = setInterval(topPingOnce, PING_INTERVAL_MS);
}

// Pausa quando a aba fica oculta (economiza CPU/RF)
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    if (topPingTimer) { clearInterval(topPingTimer); topPingTimer = null; }
  } else {
    startTopPingMonitor();
  }
});

// ===== Eventos =====
$(document).ready(function() {
  refreshChannelOptions();
  refreshAddressOptions();
  rs485RenderList(sensorMap);

  $('#type_input').on('change', function() {
    if (this.value === 'energia') {
      $('#subtype_input').prop('disabled', false);
    } else {
      $('#subtype_input').val('').prop('disabled', true);
    }
    resetTopPingState();
  });

  $('#addSensor').on('click', (ev) => {
    ev.preventDefault();
    addSensorFromInputs();
  });

  rs485FetchAndRender();

  $(document).on('change keyup', '#channel_input, #addr_input', startTopPingMonitor);
  startTopPingMonitor();

  // Proteção do botão "Gravar"
  if (typeof window.submit_form === 'function') {
    const original_submit_form = window.submit_form;

    window.submit_form = function() {
      if (sensorMap.length === 0) {
        alert(
          'Nenhum sensor RS-485 foi cadastrado.\n\n' +
          'Adicione pelo menos um sensor na tabela "Mapeamento de Sensores RS-485" ' +
          'antes de gravar as configurações de operação.'
        );
        return;
      }
      original_submit_form();
    };
  }
});

// Exponha utilitários se outros scripts precisarem
window.rs485RefreshList = updateSensorStatusList;
window.gatherSensorMap = gatherSensorMap;
window.rs485FetchAndRender = rs485FetchAndRender;
