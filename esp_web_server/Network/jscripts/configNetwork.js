function submit_form() {
    if (confirm('Gravar a configuração?')) {
        let net_config = {};
        
        try {
            net_config.apn = $("#apn").val();
            net_config.lte_user = $("#lte_user").val();
            net_config.lte_pw = $("#lte_pw").val();
            // Define http_enable e mqtt_enable com base no protocolo selecionado
            let protocol = $("input[name='protocol']:checked").val();
            net_config.http_enable = protocol === "http";
            net_config.mqtt_enable = protocol === "mqtt";
            net_config.data_server_url = $("#data_server_url").val();
            net_config.data_server_port = parseInt($("#data_server_port").val());
            net_config.data_server_path = $("#data_server_path").val();
            net_config.user = $("#user").val();
            net_config.token = $("#token").val();
            net_config.pw = $("#password").val();
            net_config.user_en = $("#user_en").prop("checked");
            net_config.token_en = $("#token_en").prop("checked");
            net_config.pw_en = $("#pw_en").prop("checked");
            net_config.mqtt_url = $("#mqtt_url").val();
            net_config.mqtt_port = parseInt($("#mqtt_port").val());
            net_config.mqtt_topic = $("#mqtt_topic").val();
                       // --- QoS MQTT (0–2) ---
            let qos = parseInt($("#mqtt_qos").val(), 10);
            if (isNaN(qos) || qos < 0 || qos > 2) {
                alert("QoS MQTT inválido. Use 0, 1 ou 2.");
                return;
            }
            net_config.mqtt_qos = qos;
            
            if (isNaN(net_config.mqtt_port) || isNaN(net_config.data_server_port)) {
                throw "Exception";
            }

 //           console.log("Enviando configuração:", net_config);

            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(net_config),
                dataType: 'text',
                success: function(data) {
                    alert("Gravação Concluída");
                },
                error: function() {
 //                   console.log("Device control failed");
                },
                processData: false,
                type: 'POST',
                url: '/configNetworkSave'
            });
        } catch (e) {
            alert("Valores incorretos!");
        }
    }
}

$(document).ready(function() {
    // Carrega configurações da rede
    $.ajax({
        type: "GET",
        url: "/configNetworkGet",
        data: "",
        cache: false,
        success: function(data) {
            let obj = JSON.parse(data);
 //           console.log("Configurações carregadas:", obj);
            $("#apn").val(obj.apn);
            $("#lte_user").val(obj.lte_user);
            $("#lte_pw").val(obj.lte_pw);
            // Define o protocolo selecionado com base em http_enable e mqtt_enable
            if (obj.http_enable) {
                $("input[name='protocol'][value='http']").prop("checked", true).attr("checked", true);
            } else if (obj.mqtt_enable) {
                $("input[name='protocol'][value='mqtt']").prop("checked", true).attr("checked", true);
            } else {
                $("input[name='protocol'][value='none']").prop("checked", true).attr("checked", true);
            }
            $("#data_server_url").val(obj.data_server_url);
            $("#data_server_port").val(obj.data_server_port);
            $("#data_server_path").val(obj.data_server_path);
            $("#user").val(obj.user);
            $("#token").val(obj.token);
            $("#password").val(obj.pw);
            $("#user_en").prop("checked", obj.user_en);
            $("#pw_en").prop("checked", obj.pw_en);
            $("#token_en").prop("checked", obj.token_en);
            $("#mqtt_url").val(obj.mqtt_url);
            $("#mqtt_port").val(obj.mqtt_port);
            $("#mqtt_topic").val(obj.mqtt_topic);
            
                  // --- QoS MQTT (0–2), default 1 se não vier do backend ---
            if (typeof obj.mqtt_qos === 'number' &&
               obj.mqtt_qos >= 0 && obj.mqtt_qos <= 2) {
               $('#mqtt_qos').val(obj.mqtt_qos);
              } else {
                      $('#mqtt_qos').val(1); // mantém o comportamento atual (QoS 1)
                      }

  //          console.log("Estado inicial - Protocolo:", $("input[name='protocol']:checked").val());
        },
        error: function() {
 //           console.log("Falha ao carregar configurações de rede");
        }
    });

    // Carrega configurações de operação
    $.ajax({
        type: "GET",
        url: "/configOpGet",
        data: "",
        cache: false,
        success: function(data) {
            let obj = JSON.parse(data);
            $("#company_label").html(obj.company);
        },
        error: function() {
   //         console.log("Falha ao carregar configurações de operação");
        }
    });
});