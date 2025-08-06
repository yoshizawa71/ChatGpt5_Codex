function submit_form(form) {
    if (confirm('Gravar a configuração?')) {
        let net_config = {};
        
        try{
            net_config.apn = $("#apn").val();
            net_config.apn_usuario = $("#gprs_user").val();
            net_config.apn_senha = $("#gprs_pw").val();
            net_config.servidor_url = $("#data_server_url").val();
            net_config.servidor_porta = parseInt($("#data_server_port").val());
            net_config.servidor_usuario = $("#user").val();
            net_config.servidor_chave = $("#key").val();
            net_config.servidor_config_url = $("#config_server_url").val();
            net_config.servidor_config_porta = parseInt($("#config_server_port").val());
            net_config.c_id = parseInt($("#config_id").val());

            if(isNaN(net_config.servidor_porta)|| isNaN(net_config.servidor_config_porta)) {
                throw "Exception";
            }

            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(net_config),
                dataType: 'text',
                success: function(data){
                    alert("Gravação Concluída");
                },
                error: function(){
                    console.log("Device control failed");
                },
                processData: false,
                type: 'POST',
                url: '/datalogger/update_config.php'
            });
        }catch(e) {
            alert("Valores incorretos!");
        }
    }
}



$( document ).ready(function() {
    var config_id = 0;
    function update_network() {
        $.ajax({
            type: "GET",
            url: "/datalogger/get_config.php",
            data: {
                "disp_id": $('#device_list option:selected').val()
            },
            cache: false,
            success: function(data){
                let obj = JSON.parse(data);
                $("#apn").val(obj[0].apn);
                $("#gprs_user").val(obj[0].apn_usuario);
                $("#gprs_pw").val(obj[0].apn_senha);
                $("#data_server_url").val(obj[0].servidor_url);
                $("#data_server_port").val(obj[0].servidor_porta);
                $("#user").val(obj[0].servidor_usuario);
                $("#key").val(obj[0].servidor_chave);
                $("#config_server_url").val(obj[0].servidor_config_url);
                $("#config_server_port").val(obj[0].servidor_config_porta);
                $("#config_id").val(obj[0].c_id);
            }
        });
    }
    
    $.ajax({
        type: "GET",
        url: "/datalogger/list_ids.php",
        data: "",
        cache: false,
        success: function(data){
            let obj = JSON.parse(data);
            $.each(obj, function (i, item) {
                $('#device_list').append($('<option>', { 
                    value: item.id,
                    text : item.id_dispositivo 
                }));
            });
            
            update_network();
        }
    });

    $("#device_list").change(function () {
        update_network();
    });
});