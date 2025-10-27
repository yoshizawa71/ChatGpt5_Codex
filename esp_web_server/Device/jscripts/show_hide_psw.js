function mostrarOcultarSenha() {
    var senha    = document.getElementById("wifi_pw_ap");
    var checkbox = document.getElementById("hide_pw");
    var eye      = document.getElementById("eye_icon");

    if (checkbox.checked) {
        senha.type = "password";
        eye.style.textDecoration = "line-through";
        localStorage.setItem("hidePassword", "true");
    } else {
        senha.type = "text";
        eye.style.textDecoration = "none";
        localStorage.setItem("hidePassword", "false");
    }
}

// Restaura o estado do checkbox, do campo e do ícone ao carregar
document.addEventListener("DOMContentLoaded", function() {
    var senha        = document.getElementById("wifi_pw_ap");
    var checkbox     = document.getElementById("hide_pw");
    var eye          = document.getElementById("eye_icon");
    var hidePassword = localStorage.getItem("hidePassword");

    if (hidePassword === "true") {
        checkbox.checked = true;
        senha.type       = "password";
        eye.style.textDecoration = "line-through";
    } else {
        checkbox.checked = false;
        senha.type       = "text";
        eye.style.textDecoration = "none";
    }
});
