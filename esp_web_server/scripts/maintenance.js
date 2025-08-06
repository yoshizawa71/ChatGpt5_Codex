$(document).ready(function() {
    // Função para enviar requisições AJAX
    function sendRequest(url, method, data) {
        return $.ajax({
            url: url,
            type: method,
            contentType: 'application/json',
            data: data ? JSON.stringify(data) : null,
            error: function(xhr, status, error) {
 //               console.error('Erro na requisição:', url, status, error);
 //               console.error('Resposta do servidor:', xhr.responseText);
                alert('Erro ao comunicar com o servidor: ' + (xhr.responseJSON?.message || error));
            }
        });
    }

    // Função para resetar o estado visual dos botões de calibração
    function resetCalibrationButtons() {
        $('#calibrate-factor').removeClass('active-button');
    }

    // Carregar estado inicial da calibração
function loadCalibrationState() {
    var selectedSensor = $('#sensor_select').val();
    var selectedUnit   = $('#unit_select').val();

    // ===== PATCH GPT-4.1: enviar sensor e unit como query params =====
    $.get('/configMaintGet', {
        sensor: selectedSensor,
        unit:   selectedUnit
    })
    // ===== FIM PATCH =====
    .done(function(data) { 
 
       if (data.erro) {
            alert(data.erro + ". Calibração não ativada.");
            $('#ativar_cali').prop('checked', false);
            $('#estado').text('Ativar');
            // não faz o resto – sai
            return;
        }
//        console.log('Dados brutos recebidos do /configMaintGet:', JSON.stringify(data));
        $('#ativar_cali').prop('checked', data.ativar_cali);
        $('#estado').text(data.ativar_cali ? 'Ativado' : 'Ativar');
        var sensorValue = (data.sensor_selected_value !== undefined)
            ? data.sensor_selected_value.toString()
            : '[Aguardando dados]';
//        console.log('Valor após atribuição:', sensorValue);
        $('#sensor_value').text(sensorValue);
        $('#sensor_value').toggleClass('active-value',
            sensorValue !== '[Aguardando dados]' &&
            sensorValue !== '[Desativado]' &&
            sensorValue !== '[Sem leitura]'
        );
    })
    .fail(function(xhr, status, error) {
 //       console.error('Erro ao carregar estado de calibração:', status, error);
    });
}

    // Carregar estado inicial do relé
    function loadRelayState() {
        sendRequest('/rele_device', 'GET').done(function(data) {
            var isActive = data.active;
            $('#btnRele').val(isActive ? 'Desligar' : 'Ligar');
            $('#btnRele').toggleClass('active-button', isActive && $('#btnRele').val() === 'Desligar');
            if ($('#btnRele').val() === 'Ligar') {
                $('#btnRele').removeClass('active-button');
            }
        }).fail(function() {
            $('#btnRele').val('Ligar');
            $('#btnRele').removeClass('active-button');
        });
    }

    // Atualizar sensores em tempo real
    let updateInterval = null;

function startSensorUpdates() {
  // se já está rodando, não reinicia
  if (updateInterval) return;

  updateInterval = setInterval(() => {
    // se o usuário desmarcou a calibração, para o loop
    if (!$('#ativar_cali').is(':checked')) {
      stopSensorUpdates();
      return;
    }

    const selectedSensor = $('#sensor_select').val();
    const selectedUnit   = $('#unit_select').val();

    $.get('/configMaintGet', { sensor: selectedSensor, unit: selectedUnit })
      .done(function(data) {
        console.log('Polling /configMaintGet →', data); 
     
        // 1) Se o backend mandou erro ou ativar_cali=false, desativa tudo
        if (!data.ativar_cali || data.erro) {
          alert((data.erro || 'Calibração desativada') + '.');
          $('#ativar_cali').prop('checked', false);
          $('#estado').text('Ativar');
          stopSensorUpdates();
          resetCalibrationButtons();
          $('#sensor_value')
            .text('[Desativado]')
            .removeClass('active-value');
          return;
        }

        // 2) Se o backend sinalizou falta de leitura, também desativa
        if (data.sensor_selected_value === '[Sem leitura]') {
          alert('Sensor desconectado ou sem leitura. Calibração desativada.');
          $('#ativar_cali').prop('checked', false);
          $('#estado').text('Ativar');
          stopSensorUpdates();
          resetCalibrationButtons();
          $('#sensor_value')
            .text('[Desativado]')
            .removeClass('active-value');
          return;
        }

        // 3) Caso contrário, mostra o valor e aplica estilo “ativo”
        $('#sensor_value')
          .text(data.sensor_selected_value)
          .toggleClass('active-value',
            data.sensor_selected_value !== '[Aguardando dados]'
          );
      })
      .fail(function() {
//        console.error('Falha no polling de calibração');
        stopSensorUpdates();
        $('#sensor_value')
          .text('[Desativado]')
          .removeClass('active-value');
      });
  }, 1000);
}
       
    function stopSensorUpdates() {
   if (updateInterval !== null) {
       clearInterval(updateInterval);
       updateInterval = null;
   }
 }
    // Inicializar
    loadCalibrationState();
    loadRelayState();
    if ($('#ativar_cali').is(':checked')) {
        startSensorUpdates();
    }

    // Manipular mudança no seletor de sensor ou unidade
    $('#sensor_select, #unit_select').on('change', function() {
        loadCalibrationState();
        updateRefOptions($('#unit_select').val());
    });

    // Manipular toggle "Ativar"
    $('#ativar_cali').on('change', function() {
        var isActive = $(this).is(':checked');
        var selectedSensor = $('#sensor_select').val();
        var selectedUnit = $('#unit_select').val();
        sendRequest('/configMaintSave', 'POST', {
            ativar_cali: isActive,
            sensor: selectedSensor,
            unit: selectedUnit
        }).done(function(response) {
           // ===== PATCH GPT-4.1: Tratamento de erro de calibração pelo backend =====
        let resp = (typeof response === "string") ? JSON.parse(response) : response;

        if (resp.ativar_cali === false && resp.erro) {
            alert(resp.erro + ". Calibração não ativada.");
            $('#ativar_cali').prop('checked', false); // Garante desativação visual
            $('#estado').text('Ativar');
            stopSensorUpdates();
            resetCalibrationButtons();
            loadCalibrationState();
            return; // Sai da função, não faz o resto do fluxo
        }
        // ===== FIM PATCH GPT-4.1 =====
        
            $('#estado').text(isActive ? 'Ativado' : 'Ativar');
            if (isActive) {
                startSensorUpdates();
            } else {
                stopSensorUpdates();
                resetCalibrationButtons();
            }
            loadCalibrationState();
        }).fail(function() {
            $(this).prop('checked', !isActive);
        });
    });

    // Manipular botão "Salvar Ponto de Calibração"
    $('#calibrate-factor').on('click', function() {
        var $this = $(this);
        if (!$('#ativar_cali').is(':checked')) {
            alert('Por favor, ative a calibração primeiro.');
            return;
        }
        var sensorValue = $('#sensor_value').text();
 //       console.log('Valor do sensor:', sensorValue);
        if (sensorValue === '[Sem leitura]') {
            alert('Sensor desconectado ou danificado. Verifique a conexão e tente novamente.');
            return;
        }
        var selectedSensor = $('#sensor_select').val();
        var selectedRef = $('#ref_select').val();
        var selectedUnit = $('#unit_select').val();
        $this.addClass('active-button');
        sendRequest('/configMaintSave', 'POST', {
            ativar_cali: $('#ativar_cali').is(':checked'),
            sensor: selectedSensor,
            unit: selectedUnit,
            sensor1_refs: selectedSensor === '1' ? { [selectedRef]: { value: sensorValue, unit: selectedUnit } } : undefined,
            sensor2_refs: selectedSensor === '2' ? { [selectedRef]: { value: sensorValue, unit: selectedUnit } } : undefined
        }).done(function() {
            alert('Ponto de calibração salvo com sucesso!');
            setTimeout(function() {
                $this.removeClass('active-button');
            }, 500);
        }).fail(function() {
            $this.removeClass('active-button');
        });
    });

    // Manipular botão "Ligar/Desligar" (Teste do Relé)
    $('#btnRele').on('click', function(e) {
        e.preventDefault();
        var $this = $(this);
        var isActive = !$this.hasClass('active-button');
        $this.toggleClass('active-button', isActive);
        $this.val(isActive ? 'Desligar' : 'Ligar');
        sendRequest('/rele_device', 'POST', { active: isActive }).fail(function() {
            $this.toggleClass('active-button', !isActive);
            $this.val(isActive ? 'Ligar' : 'Desligar');
        });
    });

    const refBaseValues = [0, 5, 10, 15, 20];

    function convertValue(value, unit) {
        switch (unit) {
            case 'mca':
                return (value * 10.1972).toFixed(2);
            case 'psi':
                return (value * 14.5038).toFixed(2);
            case 'bar':
            default:
                return value.toFixed(2);
        }
    }

    function updateRefOptions(unit) {
        const $refSelect = $('#ref_select');
        $refSelect.empty();
        refBaseValues.forEach(val => {
            const converted = convertValue(val, unit);
            $refSelect.append(`<option value="${val}">${converted} ${unit}</option>`);
        });
    }

    // Inicializa com a unidade padrão
    updateRefOptions($('#unit_select').val());

    // Atualiza ao trocar a unidade
    $('#unit_select').on('change', function() {
        const selectedUnit = $(this).val();
        updateRefOptions(selectedUnit);
    });
});

function loadCalibrationState() {
  const selectedSensor = $('#sensor_select').val();
  const selectedUnit   = $('#unit_select').val();

  $.get('/configMaintGet', { sensor: selectedSensor, unit: selectedUnit })
//    .done(function(data) {
      .done(function(data, textStatus, jqXHR) {
      // 🛠️ Aqui você vê exatamente o JSON cru que chegou do back:
      console.log('>> RAW /configMaintGet:', jqXHR.responseText);
      // 1) Se veio erro do backend OU leitura inválida ("[Sem leitura]")
      if (data.erro || data.sensor_selected_value === '[Sem leitura]') {
        const msg = data.erro
          ? data.erro + '. Calibração não ativada.'
          : 'Sensor desconectado ou leitura inválida. Calibração desativada.';

        alert(msg);
        $('#ativar_cali').prop('checked', false);
        $('#estado').text('Ativar');

        stopSensorUpdates();
        resetCalibrationButtons();

        // força o visor para "[Desativado]"
        $('#sensor_value')
          .text('[Desativado]')
          .removeClass('active-value');

        return;
      }

      // 2) Atualiza toggle e texto do botão
      $('#ativar_cali').prop('checked', data.ativar_cali);
      $('#estado').text(data.ativar_cali ? 'Ativado' : 'Ativar');

      // 3) Se a calibração estiver desligada, exibe "[Desativado]" e sai
      if (!data.ativar_cali) {
        stopSensorUpdates();
        $('#sensor_value')
          .text('[Desativado]')
          .removeClass('active-value');
        return;
      }

      // 4) Aqui só chegam valores válidos
      const sensorValue = data.sensor_selected_value || '[Aguardando dados]';

      $('#sensor_value')
        .text(sensorValue)
        .toggleClass('active-value',
          sensorValue !== '[Aguardando dados]' &&
          sensorValue !== '[Desativado]'
        );

      // 5) Garante que o polling de atualização automática esteja rodando
      startSensorUpdates();
    })
    .fail(function() {
 //     console.error('Erro ao carregar estado de calibração');
      stopSensorUpdates();
      $('#sensor_value')
        .text('[Desativado]')
        .removeClass('active-value');
    });
}
