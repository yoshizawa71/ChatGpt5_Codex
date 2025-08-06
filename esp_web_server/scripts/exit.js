$(document).ready(function() {
  // dispara o exitDevice assim que a página carrega
  $.ajax({
    type: "POST",
    url: "/exitDevice",
    cache: false
  }).done(function() {
    console.log("Exit executado com sucesso");

    // altera a URL no browser para "/" sem recarregar a página
    if (window.history && history.replaceState) {
      history.replaceState(null, '', '/');
    }
  }).fail(function(xhr, status, err) {
    console.error("Erro ao sair:", status, err);
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