// configRS485.js
// UI RS-485: formulário para adicionar e lista de sensores com status

// ===== Config =====
const RS485_MAX_SENSORS = 10;
const CHANNEL_OPTIONS = [3,4,5,6,7,8,9,10,11,12]; // ajuste conforme seu hardware

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

function gatherSensorMap() { return sensorMap.slice(); }

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

async function deleteSensorBackend(ch, addr) {
  const res = await fetch(`/rs485ConfigDelete?channel=${encodeURIComponent(ch)}&address=${encodeURIComponent(addr)}`);
  const j = await res.json().catch(() => ({}));
  if (!res.ok || !j || j.ok !== true) {
    throw new Error((j && (j.error || j.msg)) || 'Falha ao excluir no backend.');
  }
  return j;
}

function updateSensorStatusList() {
  const $list = $('#sensorStatusList').empty();
  sensorMap.sort((a,b)=>a.channel-b.channel).forEach((s) => {
    const desc = `Canal ${s.channel} – Endereço ${s.address} – ${s.type || ''}${s.subtype ? ' ('+s.subtype+')' : ''}`;

    const $line = $('<div class="sensor-line">');
    const $desc = $('<span class="sensor-desc">').text(desc);
    const $icon = $(`<span class="status-icon led circle disconnected" aria-label="status"></span>`);
    const $remove = $('<button type="button" class="rm-btn">Remover</button>').on('click', async () => {
      if (!confirm(`Remover o sensor do Canal ${s.channel}, Endereço ${s.address}?`)) return;
      $remove.prop('disabled', true).text('Removendo...');
      try {
        await deleteSensorBackend(s.channel, s.address);
        const i = sensorMap.findIndex(x => x.channel === s.channel && x.address === s.address);
        if (i >= 0) sensorMap.splice(i, 1);
        refreshChannelOptions();
        updateSensorStatusList();
      } catch (e) {
        alert(e && e.message ? e.message : 'Falha ao remover.');
        $remove.prop('disabled', false).text('Remover');
      }
    });

    $line.append($desc, $('<span>').append($icon, ' ', $remove));
    $list.append($line);

    // Ping único por item da lista (não em loop) para colorir
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

// ===== RS-485 Ping Helper (auto) =====
const PING_DEBOUNCE_MS = 250;
const BUSY_RETRY_MS    = 250;

let pingTimer = null;
let retryTimer = null;
let lastPingAlive = false;
let lastPingTarget = { channel: null, address: null };

function setPingStatus(text) {
  const $status = $('#rs485_status');
  if ($status.length) $status.text(text || '');
}

function enableAddButton(enabled) {
  const $btn = $('#addSensor');
  if ($btn.length) $btn.prop('disabled', !enabled);
}

function readFormState() {
  const chRaw = $('#channel_input').val();
  const chParsed = Number.parseInt(chRaw, 10);
  const addrRaw = $('#addr_input').val();
  const addrParsed = parseAddr(addrRaw);
  return {
    channel : Number.isInteger(chParsed) ? chParsed : NaN,
    address : Number.isInteger(addrParsed) ? addrParsed : NaN,
    type    : String($('#type_input').val() || '').trim(),
    subtype : String($('#subtype_input').val() || '').trim(),
  };
}

function resetPingState(message) {
  if (pingTimer) { clearTimeout(pingTimer); pingTimer = null; }
  if (retryTimer) { clearTimeout(retryTimer); retryTimer = null; }
  lastPingAlive = false;
  lastPingTarget = { channel: null, address: null };
  enableAddButton(false);
  if (typeof message === 'string') setPingStatus(message);
}

function schedulePing(delayMs) {
  if (pingTimer) { clearTimeout(pingTimer); pingTimer = null; }
  if (retryTimer) { clearTimeout(retryTimer); retryTimer = null; }
  enableAddButton(false);
  lastPingAlive = false;
  lastPingTarget = { channel: null, address: null };
  const wait = typeof delayMs === 'number' ? Math.max(0, delayMs) : 0;
  pingTimer = setTimeout(doPingOnce, wait);
}

function doPingOnce() {
  pingTimer = null;
  const form = readFormState();

  enableAddButton(false);
  lastPingAlive = false;

  if (!Number.isInteger(form.channel) || form.channel <= 0 ||
      !Number.isInteger(form.address) || form.address < 1 || form.address > 247) {
    setPingStatus('Informe canal e endereço…');
    return;
  }

  setPingStatus('Aguardando sensor…');
  lastPingTarget = { channel: form.channel, address: form.address };

  const url = `/rs485Ping?channel=${encodeURIComponent(form.channel)}&address=${encodeURIComponent(form.address)}`;
  fetch(url, { cache: 'no-store' })
    .then((res) => {
      if (!res.ok) throw new Error('HTTP error');
      return res.json();
    })
    .then((data) => {
      const current = readFormState();
      if (current.channel !== form.channel || current.address !== form.address) {
        return; // campos alterados durante o ping; ignora resultado antigo
      }

      if (data && data.busy) {
        setPingStatus('Barramento ocupado, tentando…');
        retryTimer = setTimeout(() => {
          retryTimer = null;
          doPingOnce();
        }, BUSY_RETRY_MS);
        return;
      }

      if (data && data.alive) {
        setPingStatus('Sensor detectado!');
        enableAddButton(true);
        lastPingAlive = true;
      } else {
        setPingStatus('Não detectado');
        enableAddButton(false);
        lastPingAlive = false;
      }
    })
    .catch(() => {
      const current = readFormState();
      if (current.channel !== form.channel || current.address !== form.address) {
        return;
      }
      setPingStatus('Falha no ping');
      enableAddButton(false);
      lastPingAlive = false;
    });
}

function onPingRelevantChange() {
  schedulePing(PING_DEBOUNCE_MS);
}

// ====== Formulário: adicionar sensor ======
async function addSensorFromInputs() {
  if (sensorMap.length >= RS485_MAX_SENSORS) {
    alert(`Limite de ${RS485_MAX_SENSORS} sensores atingido.`);
    return;
  }

  const ch = parseInt($('#channel_input').val(), 10);
  const addr = parseAddr($('#addr_input').val());
  const type = $('#type_input').val();
  const subtype = $('#subtype_input').val();

  if (!ch) { alert('Selecione o canal.'); return; }
  if (!Number.isInteger(addr) || addr < 1 || addr > 247) { alert('Endereço inválido (1..247).'); return; }
  if (!type) { alert('Selecione o tipo.'); return; }
  if (type !== 'energia') { $('#subtype_input').val(''); }

  if (sensorMap.some(s => s.channel === ch))  { alert('Canal já utilizado.');   return; }
  if (sensorMap.some(s => s.address === addr)) { alert('Endereço já utilizado.'); return; }

  if (!lastPingAlive ||
      lastPingTarget.channel !== ch ||
      lastPingTarget.address !== addr) {
    alert('Aguardando detecção do sensor (status "Sensor detectado!").');
    return;
  }

  // Chamada ao backend para registrar/persistir
  let ok = false, errMsg = '';
  // Chama o endpoint de persistência; back-end responde sempre {"ok":true}
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

  // Sucesso: adiciona local e atualiza
  sensorMap.push({ channel: ch, address: addr, type, subtype });
  refreshChannelOptions();
  updateSensorStatusList();

  // Limpa form
  $('#channel_input').val('');
  $('#addr_input').val('');
  $('#type_input').val('');
  $('#subtype_input').val('').prop('disabled', true);

  resetPingState('Aguardando sensor…');
  schedulePing(PING_DEBOUNCE_MS);
}

// ===== Eventos =====
$(document).ready(function() {
  refreshChannelOptions();

  $('#type_input').on('change', function() {
    if (this.value === 'energia') $('#subtype_input').prop('disabled', false);
    else $('#subtype_input').val('').prop('disabled', true);
  });

  $('#addSensor').on('click', (ev) => { ev.preventDefault(); addSensorFromInputs(); });

  // Carrega sensores já salvos
  $.getJSON('/rs485ConfigGet')
    .done(data => {
      if (data && Array.isArray(data.sensors)) {
        data.sensors.forEach(s => {
          if (!s) return;
          if (typeof s.channel !== 'number' || typeof s.address !== 'number') return;
          if (sensorMap.length >= RS485_MAX_SENSORS) return;

          if (!sensorMap.some(x => x.channel === s.channel) &&
              !sensorMap.some(x => x.address === s.address)) {
            sensorMap.push({
              channel: s.channel,
              address: s.address,
              type: s.type || '',
              subtype: s.subtype || ''
            });
          }
        });
        refreshChannelOptions();
        updateSensorStatusList();
      }
    })
    .fail(() => { /* ok se não existir */ });

  // Auto ping inicial e reações do formulário
  $('#channel_input').on('change', onPingRelevantChange);
  $('#addr_input').on('input', onPingRelevantChange);
  $('#type_input').on('change', onPingRelevantChange);
  $('#subtype_input').on('change', onPingRelevantChange);

  resetPingState('Aguardando sensor…');
  schedulePing(200);
});

// Exponha utilitários se outros scripts precisarem
window.rs485RefreshList = updateSensorStatusList;
window.gatherSensorMap = gatherSensorMap;
