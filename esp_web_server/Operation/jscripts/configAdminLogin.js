function submit_form(form) {
    let login_config = {};
    
    try{
        login_config.key = $("#key").val();

        $.ajax({
            contentType: 'application/json',
            data: JSON.stringify(login_config),
            dataType: 'text',
            success: function(data){
                obj = JSON.parse(data);
                if(obj.login) {
                    document.location.reload();
                } else {
                    alert("Acesso Negado!");
                }
            },
            error: function(){
                alert("Acesso Negado!");
            },
            processData: false,
            type: 'POST',
            url: '/configOpLogin'
        });
    }catch(e) {
        alert("Valores incorretos!");
    }
}

$( document ).ready(function() {
    $("#key").val("");
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