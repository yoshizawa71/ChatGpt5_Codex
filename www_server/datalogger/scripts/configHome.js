$( document ).ready(function() {
    function update_home() {
        $.ajax({
            type: "GET",
            url: "/datalogger/get_config.php",
            data: {
                "disp_id": $('#device_list option:selected').val()
            },
            cache: false,
            success: function(data){
                let obj = JSON.parse(data);
                $("#apn").html(obj[0].apn);
                $("#data_server_url").html(obj[0].servidor_url);
                $("#user").html(obj[0].servidor_usuario);
                $("#scale").html(obj[0].escala);
                $("#send_period").html(obj[0].frequencia);
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
            
            update_home();
        }
    });

    $("#device_list").change(function () {
        update_home();
    });
});