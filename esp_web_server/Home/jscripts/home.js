$(document).ready(function() {
    let currentDateTime = null;

    function fetchTime() {
        $.getJSON("/get_time", function(data) {
            if (data.date && data.time) {
                const [day, month, year] = data.date.split('/');
                const timeParts = data.time.split(':');
                const hour = timeParts[0];
                const minute = timeParts[1];
                const second = timeParts[2] || "00";
                currentDateTime = new Date(`${year}-${month}-${day}T${hour}:${minute}:${second}`);
                updateClock();
            }
        }).fail(function() {
  //          console.error("Erro ao obter horário do servidor");
        });
    }

    function updateClock() {
        if (currentDateTime) {
            currentDateTime.setSeconds(currentDateTime.getSeconds() + 1);
            const timeStr = currentDateTime.toLocaleTimeString('pt-BR');
            $("#systemTime").text(timeStr);
        }
    }

    fetchTime();
    setInterval(updateClock, 1000);

    $.ajax({
        type: "GET",
        url: "/configDeviceGet",
        data: "",
        cache: false,
        success: function(data) {
            let obj = JSON.parse(data);
            $("#device").html(obj.id);
            $("#send_period").html(obj.send_period);
            $("#name").html(obj.name);
            $("#phone").html(obj.phone);
        }
    });

    $.ajax({
        type: "GET",
        url: "/configNetworkGet",
        data: "",
        cache: false,
        success: function(data) {
            let obj = JSON.parse(data);
            $("#apn").html(obj.apn);
            $("#data_server_url").html(obj.data_server_url);
            $("#user").html(obj.user);
        }
    });

    $.ajax({
        type: "GET",
        url: "/configOpGet",
        data: "",
        cache: false,
        success: function(data) {
            let obj = JSON.parse(data);
            $("#company_label").html(obj.company);
            $("#serial_number").html(obj.serial_number);
            $("#csq").html(obj.csq);
            
            let bat = obj.battery;

            if (typeof bat === "number") {
                // veio como número do ESP
                $("#battery").html(bat.toFixed(2) + " V");
            } else if (typeof bat === "string" && bat.trim() !== "") {
                // se por algum motivo vier string, tenta converter
                let num = parseFloat(bat.replace(",", "."));
                if (!isNaN(num)) {
                    $("#battery").html(num.toFixed(2) + " V");
                } else {
                    // fallback: mostra a string mesmo
                    $("#battery").html(bat + " V");
                }
            } else {
                $("#battery").html("--");
            }

            $("#last_comm").html(obj.last_comm);
        }
    });
});