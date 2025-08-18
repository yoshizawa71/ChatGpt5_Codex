// configRS485.js
// UI RS-485: uma linha de entrada + lista de sensores com status

// ===== Config =====
const RS485_MAX_SENSORS = 10;
const CHANNEL_OPTIONS = [3,4,5,6,7,8,9,10,11,12]; // canais válidos

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

/** Cria/atualiza a lista visual com status (● conectado/desconectado) */
function updateSensorStatusList() {
  const $list = $('#sensorStatusList').empty();
  sensorMap
    .sort((a,b)=>a.channel-b.channel)
    .forEach((s, idx) => {
      const desc = `Canal ${s.channel} – Endereço ${s.address} – ${s.type}${s.subtype ? ' ('+s.subtype+')' : ''}`;

      const $line = $('<div class="sensor-line">');
      const $desc = $('<span class="sensor-desc">').text(desc);
      const $icon = $('<span class="status-icon disconnected">').text('●');
      const $remove = $('<button type="button" class="rm-btn">Remover</button>')
        .on('click', () => {
          // remove do array
          const i = sensorMap.findIndex(x => x.channel === s.channel && x.address === s.address);
          if (i >= 0) sensorMap.splice(i,1);
          refreshChannelOptions();
          updateSensorStatusList();
        });

      $line.append($desc, $('<span>').append($icon, ' ', $remove));
      $list.append($line);

      // Faz ping para colorir o status
      $.getJSON(`/rs485Ping?channel=${s.channel}&address=${s.address}`)
        .done(res => {
          $icon
            .removeClass('disconnected connected')
            .addClass(res && res.alive ? 'connected' : 'disconnected');
        })
        .fail(() => {
          $icon
            .removeClass('connected')
            .addClass('disconnected');
        });
    });
}

/** Adiciona o sensor da linha de entrada à lista/array */
function addSensorFromInputs() {
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

  // duplicidade
  if (sensorMap.some(s => s.channel === ch)) {
    alert('Canal já utilizado.'); return;
  }
  if (sensorMap.some(s => s.address === addr)) {
    alert('Endereço já utilizado.'); return;
  }

  // adiciona
  sensorMap.push({ channel: ch, address: addr, type, subtype });

  // atualiza UI
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
  $('#addSensor').on('click', addSensorFromInputs);

  // (Opcional) Carrega configuração salva para preencher a lista ao abrir a página
  // Se você criou os endpoints separados /rs485ConfigGet:
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
