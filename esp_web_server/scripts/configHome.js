$( document ).ready(function() {
    $.ajax({
        type: "GET",
        url: "/configDeviceGet",
        data: "",
        cache: false,
        success: function(data){
            let obj = JSON.parse(data);
            $("#device").html(obj.id);
            $("#send_period").html(obj.send_period);
            $("#name").html(obj.name);
            $("#phone").html(obj.phone);
        }
    });
});

$( document ).ready(function() {
    $.ajax({
        type: "GET",
        url: "/configNetworkGet",
        data: "",
        cache: false,
        success: function(data){
            let obj = JSON.parse(data);
            $("#apn").html(obj.apn);
            $("#data_server_url").html(obj.data_server_url);
            $("#user").html(obj.user);
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
            $("#serial_number").html(obj.serial_number);
            $("#csq").html(obj.csq);
            $("#battery").html(obj.battery);
            $("#last_comm").html(obj.last_comm);
        }
    });
});