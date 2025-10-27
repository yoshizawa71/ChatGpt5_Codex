// scripts/configDevice.js

;(function($) {
  // opções fixas de envio (valor em minutos, label para exibição)
  const SEND_OPTIONS = [
    {value: 1,   label: '1 min'},
    {value: 5,   label: '5 min'},
    {value: 10,  label: '10 min'},
    {value: 15,  label: '15 min'},
    {value: 30,  label: '30 min'},
    {value: 60,  label: '1 h'},
    {value: 120, label: '2 h'},
    {value: 240, label: '4 h'},
    {value: 480, label: '8 h'},
    {value: 720, label: '12 h'}
  ];

  // --- Limitar inputs de hora (0–23) em tempo real ---
  ['#send_time1', '#send_time2', '#send_time3', '#send_time4'].forEach(sel => {
    $(sel)
      .on('input', function() {
        const v = parseInt(this.value, 10);
        if (isNaN(v)) {
          this.value = this.value === '' ? '' : '';
        } else if (v < 0) {
          this.value = '0';
        } else if (v > 23) {
          this.value = '23';
        } else {
          this.value = v;
        }
      })
      .on('keypress', function(e) {
        const c = e.which;
        if (c < 48 || c > 57) e.preventDefault();
      });
  });

  // --- Atualiza dropdown de frequência de envio segundo deep sleep ---
  function updateSendFreqOptions() {
    const min = parseInt($('#deep_sleep_period').val(), 10) || 1;
    const $sel = $('#send_freq');
    const current = parseInt($sel.val(), 10);

    $sel.empty();
    SEND_OPTIONS.filter(opt => opt.value >= min)
      .forEach(opt => $sel.append($('<option>').val(opt.value).text(opt.label)));

    if (current >= min && SEND_OPTIONS.some(opt => opt.value === current)) {
      $sel.val(current);
    } else {
      const first = SEND_OPTIONS.find(opt => opt.value >= min).value;
      $sel.val(first);
    }
  }

  // --- Atualiza texto e cor de status do toggle ---
  function updateDeviceStatus(isOn) {
    $('#device_status')
      .text(isOn ? 'Ligado' : 'Desligado')
      .removeClass(isOn ? 'off' : 'on')
      .addClass(isOn ? 'on' : 'off');
  }

  // === Carregamento inicial ===
  $(function() {
    // GET configDevice
    $.getJSON('/configDeviceGet', function(obj) {
      // popula campos básicos
      $('#device').val(obj.id);
      $('#name').val(obj.name);
      $('#phone').val(obj.phone);
      $('#ssid_ap').val(obj.ssid_ap);
      $('#wifi_pw_ap').val(obj.wifi_pw_ap);
      $('#activate_sta').prop('checked', obj.activate_sta);
      $('#ssid_sta').val(obj.ssid_sta);
      $('#wifi_pw_sta').val(obj.wifi_pw_sta);

      // modo de envio
      $('#chk_freq_send_data').prop('checked', obj.send_mode === 'freq');
      $('#chk_time_send_data').prop('checked', obj.send_mode === 'time');

      // deep sleep + dropdown
      $('#deep_sleep_period').val(obj.deep_sleep_period);
      updateSendFreqOptions();

      // preenche frequência e horários
      $('#send_freq').val(obj.send_period);
      ['#send_time1','#send_time2','#send_time3','#send_time4'].forEach((sel,i) => {
        const t = (obj.send_times && Array.isArray(obj.send_times)) ? obj.send_times[i] : null;
        $(sel).val((t == null || t < 0 || t > 23) ? '' : t);
      });

      $('#save_pulse_zero').prop('checked', obj.save_pulse_zero);
      $('#volume').val(obj.scale);
      $('#flow_rate').text(obj.flow_rate);
      $('#date').val(obj.date);
      $('#time').val(obj.time);
      $('#config_factory').prop('checked', obj.finished_factory);
      $('#always_on').prop('checked', obj.always_on);

      // **Novo: popula toggle e status**
      $('#device_toggle').prop('checked', obj.device_active);
      updateDeviceStatus(obj.device_active);
    }).fail(() => {
      alert('Falha ao carregar configurações do dispositivo.');
    });

    // GET configOp
    $.get('/configOpGet', function(data) {
      $('#company_label').html(JSON.parse(data).company);
    });

    // Checkboxes mutuamente exclusivos (radio-like)
    $('#chk_freq_send_data').on('change', function() {
      if ($(this).prop('checked')) {
        $('#chk_time_send_data').prop('checked', false);
      } else {
        $(this).prop('checked', true);
      }
    });
    $('#chk_time_send_data').on('change', function() {
      if ($(this).prop('checked')) {
        $('#chk_freq_send_data').prop('checked', false);
      } else {
        $(this).prop('checked', true);
      }
    });

    // Muda deep_sleep_period
    $('#deep_sleep_period').on('change', updateSendFreqOptions);

    // **Novo: listener do toggle**
    $('#device_toggle').on('change', function() {
      updateDeviceStatus(this.checked);
    });
  });

  // === Função de submissão ===
  window.submit_form = function() {
    if (!confirm('Gravar a configuração?')) return;

    // monta array send_times, permitindo vazio
    const send_times = [];
    for (let i = 1; i <= 4; i++) {
      const str = $(`#send_time${i}`).val();
      if (str === '') send_times.push(null);
      else {
        const num = parseInt(str, 10);
        if (isNaN(num) || num < 0 || num > 23) {
          alert(`Hora ${i} inválida! Digite valor entre 0 e 23 ou deixe em branco.`);
          return;
        }
        send_times.push(num);
      }
    }

    // monta objeto de envio
    const dev_config = {
      id:                $('#device').val(),
      name:              $('#name').val(),
      phone:             $('#phone').val(),
      ssid_ap:           $('#ssid_ap').val(),
      wifi_pw_ap:        $('#wifi_pw_ap').val(),
      activate_sta:      $('#activate_sta').prop('checked'),
      ssid_sta:          $('#ssid_sta').val(),
      wifi_pw_sta:       $('#wifi_pw_sta').val(),
      deep_sleep_period: parseInt($('#deep_sleep_period').val(), 10),
      send_mode:         $('#chk_freq_send_data').prop('checked') ? 'freq' : 'time',
      send_period:       parseInt($('#send_freq').val(), 10),
      send_times:        send_times,
      save_pulse_zero:   $('#save_pulse_zero').prop('checked'),
      scale:             parseInt($('#volume').val(), 10),
      date:              $('#date').val(),
      time:              $('#time').val(),
      finished_factory:  $('#config_factory').prop('checked'),
      always_on:         $('#always_on').prop('checked'),

      // **Novo: estado do toggle**
      device_active:     $('#device_toggle').prop('checked')
    };
// ===================== BLOQUEIO LIGA/FINALIZA =====================
if (dev_config.device_active && !dev_config.finished_factory) {
  alert('Marque "Finalizado" antes de deixar o Datalogger LIGADO.\n' +
        'A configuração não foi salva.');

  // opcional: voltar o toggle para DESLIGADO na tela
  $('#device_toggle').prop('checked', false);
  if (typeof updateDeviceStatus === 'function') {
    updateDeviceStatus(false);
  }

  return; // aborta o submit (nada é gravado)
}
    // validações
    if (dev_config.wifi_pw_ap.length && (dev_config.wifi_pw_ap.length < 8 || dev_config.wifi_pw_ap.length > 63)) {
      alert('A senha do ponto de acesso deve ter entre 8 e 63 caracteres!'); return;
    }
    if (dev_config.activate_sta && (dev_config.wifi_pw_sta.length < 8 || dev_config.wifi_pw_sta.length > 63)) {
      alert('A senha da rede Wi-Fi deve ter entre 8 e 63 caracteres!'); return;
    }
    if (!$('#chk_freq_send_data').prop('checked') && !$('#chk_time_send_data').prop('checked')) {
      alert('Selecione Modo Frequência ou Modo Tempo.'); return;
    }

    // envio AJAX
    $.ajax({
      contentType: 'application/json',
      data: JSON.stringify(dev_config),
      dataType: 'text',
      type: 'POST',
      url: '/configDeviceSave',
      success: () => alert('Gravação Concluída'),
      error:   () => alert('Erro ao gravar configuração.')
    });
  };
})(jQuery);
