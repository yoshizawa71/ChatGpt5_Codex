function submit_form(form) {
    if (confirm('Programar atualização?')) {
        let config = {};
        try{
            config.versao_firmware = $("#fw_version").val();

            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(config),
                dataType: 'text',
                success: function(data){
                    alert("Atualização Programada");
                },
                error: function(){
                    console.log("Device control failed");
                },
                processData: false,
                type: 'POST',
                url: '/datalogger/novo_fw.php'
            });
        }catch(e) {
            alert("Valores incorretos!");
        }
    }
}