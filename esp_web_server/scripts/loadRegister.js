function load_file(value) {
    $( document ).ready(function() {
        if(value == 0)
        {
            $("#registers").html("");
            $("#delete").prop('disabled', false);
            $("#downloadBtt").prop('disabled', false);
        }

        $.ajax({
            type: "GET",
            url: "/loadRegisters?last_byte="+value,
            data: "",
            cache: false,
            success: function(data){
                let obj = JSON.parse(data);

                $("#registers").html($("#registers").html() + obj.chunk);
                if(obj.end == false)
                {
                    load_file(obj.last_byte);
                }

            }
        });
    });
}

function delete_file() {
    if (confirm('Apagar o arquivo?')) {
        document.getElementById('registers').value = "";
    $( document ).ready(function() {
        
        $.ajax({
            type: "GET",
            url: "/deleteRegisters",
            data: "",
            cache: false,
            success: function(data){
                let obj = JSON.parse(data);

                $("#registers").html($("#registers").html() + obj.chunk);

            }
        });
    });
  }
}
function doDownload() {
    function dataUrl(data) {
      return "data:x-application/xml;charset=utf-8," + escape(data);
    }
    var downloadLink = document.createElement("a");
    downloadLink.href = dataUrl($("#registers").val());
    downloadLink.download = "registros.csv";

    document.body.appendChild(downloadLink);
    downloadLink.click();
    document.body.removeChild(downloadLink);
  }

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