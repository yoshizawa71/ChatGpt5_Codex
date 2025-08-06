function submit_form(form) {
    if (confirm('Gravar a configuração?')) {
        let dev_config = {};
        try{
            dev_config.id_dispositivo = $('#device_list option:selected').text();
            dev_config.frequencia = parseInt($("#send_period").val());
            // dev_config.date = $("#date").val();
            // dev_config.time = $("#time").val();
            dev_config.escala = parseFloat($("#scale").val());
            dev_config.finalizado_processo_fabrica = $("#config_factory").prop("checked") ? 1 : 0;
            dev_config.id = $('#device_list option:selected').val();

            if(isNaN(dev_config.frequencia)|| isNaN(dev_config.escala)) {
                throw "Exception";
            }

            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(dev_config),
                dataType: 'text',
                success: function(data){
                    alert("Gravação Concluída");
                },
                error: function(){
                    console.log("Device control failed");
                },
                processData: false,
                type: 'POST',
                url: '/datalogger/update_disp.php'
            });
        }catch(e) {
            alert("Valores incorretos!");
        }
    }
}




$( document ).ready(function() {
    function update_device() {
        $.ajax({
            type: "GET",
            url: "/datalogger/get_config.php",
            data: {
                "disp_id": $('#device_list option:selected').val()
            },
            cache: false,
            success: function(data){
                let obj = JSON.parse(data);
                $("#device").val(obj[0].id_dispositivo);
                $("#send_period").val(obj[0].frequencia);
                $("#date").val(obj[0].date);
                $("#time").val(obj[0].time);
                $("#scale").val(obj[0].escala);
                $("#config_factory").prop("checked", parseInt(obj[0].finalizado_processo_fabrica));
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
            
            update_device();
        }
    });

    $("#device_list").change(function () {
        update_device();
    });
});