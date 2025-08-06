$( document ).ready(function() {
    $.ajax({
        type: "POST",
 //       url: "/rebootDevice",
        data: "",
        cache: false,
        success: function(data){
            console.log("Reboot");
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