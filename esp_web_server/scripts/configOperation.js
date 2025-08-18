
function submit_form(form) {
    if (confirm('Gravar a configuração?')) {
        let op_config = {};

        try{
            op_config.serial_number = $("#serial_number").val();
            op_config.company = $("#company").val();
            op_config.ds_start = $("#ds_start").val();
            op_config.ds_end = $("#ds_end").val();
            op_config.count_reset = $("#count_reset").prop("checked");
            op_config.keep_alive = $("#keep_alive").val();
            op_config.log1 = $("#log1").prop("checked");
            op_config.log2 = $("#log2").prop("checked");
            op_config.post_en = $("#post_en").prop("checked");
            op_config.get_en = $("#get_en").prop("checked");
            op_config.config_server_url = $("#config_server_url").val();
            op_config.config_server_port = parseInt($("#config_server_port").val());
            op_config.config_server_path = $("#config_server_path").val();
            op_config.level_min = parseInt($("#level_min").val());
            op_config.level_max = parseInt($("#level_max").val());
            
            // === pega o mapeamento RS-485 do configRS485.js ===
            const sensors = gatherSensorMap();   // <- AQUI você chama
           
                       // 1) Salva configuração geral
            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(op_config),
                dataType: 'text',
                processData: false,
                type: 'POST',
                url: '/configOpSave',
                success: function () {
                    // 2) Depois, salva o mapeamento RS-485
                    $.ajax({
                        contentType: 'application/json',
                        data: JSON.stringify({ sensors }),
                        dataType: 'text',
                        processData: false,
                        type: 'POST',
                        url: '/rs485ConfigSave',
                        success: function () {
                            alert("Gravação Concluída");
                        },
                        error: function () {
                            console.log("Falha ao gravar RS-485");
                            alert("Falha ao gravar mapeamento RS-485.");
                        }
                    });
                },
                error: function () {
                    console.log("Device control failed");
                    alert("Falha ao gravar configuração geral.");
                }
            });
        } catch (e) {
            alert("Valores incorretos!");
        }
    }
}

$( document ).ready(function() {
    $.ajax({
        type: "GET",
        url: "/configOpGet",
        data: "",
        cache: false,
        success: function(data){
            let obj = JSON.parse(data);
            $("#serial_number").val(obj.serial_number);
            $("#company").val(obj.company);
            $("#ds_start").val(obj.ds_start);
            $("#ds_end").val(obj.ds_end);
            $("#count_reset").prop("checked", obj.count_reset);
            $("#keep_alive").val(obj.keep_alive);
            $("#post_en").prop("checked", obj.post_en);
            $("#get_en").prop("checked", obj.get_en);
            $("#config_server_url").val(obj.config_server_url);
            $("#config_server_port").val(obj.config_server_port);
            $("#config_server_path").val(obj.config_server_path);
            $("#level_min").val(obj.level_min);
            $("#level_max").val(obj.level_max);
        }
        
    });

});

$( document ).ready(function() {
    $.ajax({
        type: "GET",
        url: "/configOpGet",
        data: "",
        cache: false,
        success: function(data){
            let obj = JSON.parse(data);
            $("#company_label").html(obj.company);
        }
    });
});


