// configRS485.js
// UI RS-485: uma linha de entrada + lista de sensores com status

// ===== Config =====
const RS485_MAX_SENSORS = 10;
const CHANNEL_OPTIONS = [3,4,5,6,7,8,9,10,11,12]; // canais válidos

// Forma do LED no status: 'circle' (padrão) ou 'square'
const LED_SHAPE = 'square';

// ===== Estado =====
const sensorMap = []; // { channel, address, type, subtype }

// ===== Utils =====
/** Converte string decimal ou hexadecimal (0xNN) em inteiro */
function parseAddr(s) {
  if (typeof s !== 'string') return NaN;
  const t = s.trim();
  if (!t) return NaN;
  if (t.startsWith('0x') || t.startsWith('0X')) return parseInt(t, 16);
  return parseInt(t, 10);
}

/** Retorna array atual (compatível com configOperation.js) */
function gatherSensorMap() {
  return sensorMap.slice(); // cópia raso
}

/** Atualiza as opções do dropdown de canal, desabilitando os já usados */
function refreshChannelOptions() {
  const used = new Set(sensorMap.map(s => s.channel));
  $('#channel_input option').remove(); // recria do zero
  $('#channel_input').append('<option value="">Selecione…</option>');
  CHANNEL_OPTIONS.forEach(ch => {
    const disabled = used.has(ch) ? 'disabled' : '';
    $('#channel_input').append(`<option value="${ch}" ${disabled}>${ch}</option>`);
  });
}

/** Chama backend para excluir um sensor persistido */
async function deleteSensorBackend(ch, addr) {
  const url = `/rs485ConfigDelete?channel=${encodeURIComponent(ch)}&address=${encodeURIComponent(addr)}`;
  const res = await fetch(url, { method: 'GET' });
  const j = await res.json().catch(() => ({}));
  if (!res.ok || !j || j.ok !== true) {
    const msg = (j && (j.error || j.msg)) ? String(j.error || j.msg) : 'Falha ao excluir no backend.';
    throw new Error(msg);
  }
  return j; // { ok:true, removed, remaining }
}

/** Cria/atualiza a lista visual com status (LED conectado/desconectado) */
function updateSensorStatusList() {
  const $list = $('#sensorStatusList').empty();
  sensorMap
    .sort((a,b)=>a.channel-b.channel)
    .forEach((s) => {
      const desc = `Canal ${s.channel} – Endereço ${s.address} – ${s.type}${s.subtype ? ' ('+s.subtype+')' : ''}`;

      const $line = $('<div class="sensor-line">');
      const $desc = $('<span class="sensor-desc">').text(desc);

      // LED estilizado por CSS (maior, azul vivo, com glow quando conectado)
      const $icon = $(`<span class="status-icon led ${LED_SHAPE} disconnected" aria-label="status"></span>`);

      const $remove = $('<button type="button" class="rm-btn">Remover</button>')
        .on('click', async () => {
          if (!confirm(`Remover o sensor do Canal ${s.channel}, Endereço ${s.address}?`)) return;

          $remove.prop('disabled', true).text('Removendo...');
          try {
            await deleteSensorBackend(s.channel, s.address);
            // remove local e atualiza UI
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

      // Faz ping para colorir o status (conectado = LED azul vivo)
      $.getJSON(`/rs485Ping?channel=${s.channel}&address=${s.address}`)
        .done(res => {
          $icon
            .removeClass('disconnected connected')
            .addClass(res && res.alive ? 'connected' : 'disconnected');
        })
        .fail(() => {
          $icon.removeClass('connected').addClass('disconnected');
        });
    });
}

/** Adiciona o sensor da linha de entrada à lista/array
 *  -> mantém seu fluxo atual: valida+registra no backend antes de inserir localmente */
async function addSensorFromInputs() {
  if (sensorMap.length >= RS485_MAX_SENSORS) {
    alert(`Limite de ${RS485_MAX_SENSORS} sensores atingido.`);
    return;
  }

  const ch = parseInt($('#channel_input').val(), 10);
  const addr = parseAddr($('#addr_input').val());
  const type = $('#type_input').val();
  const subtype = $('#subtype_input').val();

  // validações simples
  if (!ch) { alert('Selecione o canal.'); return; }
  if (isNaN(addr) || addr < 1 || addr > 247) { alert('Endereço inválido (1..247 ou 0x01..0xF7).'); return; }
  if (!type) { alert('Selecione o tipo de sensor.'); return; }
  if (type !== 'energia') { $('#subtype_input').val(''); } // limpa se não é energia

  // duplicidades locais
  if (sensorMap.some(s => s.channel === ch))  { alert('Canal já utilizado.');   return; }
  if (sensorMap.some(s => s.address === addr)) { alert('Endereço já utilizado.'); return; }

  // Verificação + registro no backend (conforme seu fluxo atual)
  let ok = false, errMsg = '';
  try {
    const res = await fetch('/rs485Register', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ channel: ch, address: addr, type, subtype })
    });
    const j = await res.json().catch(() => ({}));
    ok = !!(res.ok && j && j.ok === true);
    errMsg = (j && j.error) ? String(j.error) : '';
  } catch (e) {
    errMsg = 'Falha de comunicação com o backend (/rs485Register).';
  }

  if (!ok) {
    alert(errMsg || 'Não foi possível verificar/registrar o dispositivo.\n' +
                    'Possíveis causas: não plugado, driver incompatível, função Modbus incorreta.');
    return; // não registra localmente
  }

  // Sucesso: registra local e atualiza UI; LED será atualizado pelo ping
  sensorMap.push({ channel: ch, address: addr, type, subtype });

  refreshChannelOptions();
  updateSensorStatusList();

  // limpa inputs p/ próxima inserção
  $('#channel_input').val('');
  $('#addr_input').val('');
  $('#type_input').val('');
  $('#subtype_input').val('').prop('disabled', true);
}

// ===== Eventos =====
$(document).ready(function() {
  // Prepara dropdown de canal
  refreshChannelOptions();

  // Habilita/Desabilita subtipo conforme tipo
  $('#type_input').on('change', function() {
    if (this.value === 'energia') {
      $('#subtype_input').prop('disabled', false);
    } else {
      $('#subtype_input').val('').prop('disabled', true);
    }
  });

  // Botão "Adicionar Sensor"
  $('#addSensor').on('click', (ev) => { ev.preventDefault(); addSensorFromInputs(); });

  // Carrega configuração salva para preencher a lista ao abrir a página
  $.getJSON('/rs485ConfigGet')
    .done(data => {
      if (data && Array.isArray(data.sensors)) {
        data.sensors.forEach(s => {
          // sanity check
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
    .fail(() => { /* se não existir o endpoint, ignora silenciosamente */ });
});

// ===== Exponha algo global se precisar =====
// Se outro script quiser forçar atualização visual:
window.rs485RefreshList = updateSensorStatusList;
// Seu configOperation.js pode continuar usando gatherSensorMap()
window.gatherSensorMap = gatherSensorMap;
