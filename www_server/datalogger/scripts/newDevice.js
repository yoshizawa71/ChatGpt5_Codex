function submit_form(form) {
    if (confirm('Incluir novo dispositivo?')) {
        let dev = {};
        try{
            dev.id_dispositivo = $("#device_id").val();

            $.ajax({
                contentType: 'application/json',
                data: JSON.stringify(dev),
                dataType: 'text',
                success: function(data){
                    alert("Inclusão Concluída");
                },
                error: function(){
                    console.log("Device control failed");
                },
                processData: false,
                type: 'POST',
                url: '/datalogger/novo_disp.php'
            });
        }catch(e) {
            alert("Valores incorretos!");
        }
    }
}