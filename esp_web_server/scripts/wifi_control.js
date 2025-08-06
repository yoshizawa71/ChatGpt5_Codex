function updateWifiLed(status) {
 //console.log("Atualizando LED. Status recebido:", status, "Tipo:", typeof status);
  // Aceita tanto booleano quanto string "true"
  if (status === true || status === "true") {
 //console.log("Definindo background-color como verde claro");
    $('#wifi-led').css('background-color', '#00FF00');
  } else {
    //console.log("Definindo background-color como vermelho");
    $('#wifi-led').css('background-color', '#FF0000');
  }
}

function checkWifiStatus() {
//console.log("Consultando status em /status_sta");
  $.get('/status_sta', function(data) {
   //console.log("Resposta bruta de /status_sta:", data);
     if (data && typeof data.sta_connected !== 'undefined') {
      //console.log("sta_connected encontrado:", data.sta_connected);
      updateWifiLed(data.sta_connected);
    } else {
      //console.log("Resposta inválida de /status_sta:", parsedData);
      updateWifiLed(false);
    }
  }).fail(function(jqXHR, textStatus, errorThrown) {
    //console.log("Falha na requisição /status_sta:", textStatus, errorThrown);
    updateWifiLed(false);
  });
}

function toggleWifiSta() {
  var isChecked = $('#activate_sta').is(':checked');
  var ssid = $('#ssid_sta').val();
  var password = $('#wifi_pw_sta').val();

  //console.log("Checkbox mudou. Estado:", isChecked, "SSID:", ssid, "Password:", password);

  if (isChecked) {
    if (!ssid || !password) {
      //console.log("SSID ou senha vazios. Requisição não enviada.");
      alert("Por favor, preencha o SSID e a senha antes de ativar o WiFi.");
      $('#activate_sta').prop('checked', false);
      return;
    }
    if (password.length < 8) {
      //console.log("Senha muito curta. Requisição não enviada.");
      alert("A senha deve ter pelo menos 8 caracteres.");
      $('#activate_sta').prop('checked', false);
      return;
    }

    var payload = { ssid: ssid, password: password };
    //console.log("Enviando requisição POST para /connect_sta com payload:", payload);

    $.ajax({
      url: '/connect_sta',
      type: 'POST',
      contentType: 'application/json',
      data: JSON.stringify(payload),
      success: function(data) {
        //console.log("Requisição /connect_sta bem-sucedida. Resposta:", data);
        checkWifiStatus();
      },
      error: function(jqXHR, textStatus, errorThrown) {
        //console.log("Erro na requisição /connect_sta:", textStatus, errorThrown);
        alert('Erro ao tentar conectar o WiFi: ' + textStatus);
        updateWifiLed(false);
      }
    });
  } else {
    //console.log("Enviando requisição POST para /connect_sta para desconectar");
    $.ajax({
      url: '/connect_sta',
      type: 'POST',
      contentType: 'application/json',
      data: JSON.stringify({ disconnect: true }),
      success: function(data) {
        //console.log("Requisição de desconexão bem-sucedida. Resposta:", data);
        checkWifiStatus();
      },
      error: function(jqXHR, textStatus, errorThrown) {
        //console.log("Erro na requisição de desconexão:", textStatus, errorThrown);
        alert('Erro ao tentar desconectar o WiFi: ' + textStatus);
        updateWifiLed(false);
      }
    });
  }
}

$(document).ready(function() {
  //console.log("wifi_control.js carregado com sucesso");
  //console.log("Elemento #wifi-led existe?", $('#wifi-led').length > 0 ? "Sim" : "Não");

  $('#activate_sta').on('change', function() {
    //console.log("Evento de mudança no checkbox #activate_sta detectado");
    toggleWifiSta();
  });

  checkWifiStatus();
  setInterval(checkWifiStatus, 5000);
});