function aplicarVisibilidadeSenha(hide) {
    var senha = document.getElementById("wifi_pw_ap");
    var eye   = document.getElementById("eye_icon");

    if (!senha || !eye) {
        return;
    }

    if (hide) {
        // esconder senha
        senha.type = "password";
        eye.style.textDecoration = "line-through";
    } else {
        // mostrar senha
        senha.type = "text";
        eye.style.textDecoration = "none";
    }
}

function mostrarOcultarSenha() {
    var checkbox = document.getElementById("hide_pw");
    if (!checkbox) {
        return;
    }

    var hide = checkbox.checked;  // MARCADO = esconder
    aplicarVisibilidadeSenha(hide);

    // persiste estado
    localStorage.setItem("hidePasswordAp", hide ? "true" : "false");
}

// Restaura o estado ao carregar a página
document.addEventListener("DOMContentLoaded", function() {
    var checkbox = document.getElementById("hide_pw");

    if (!checkbox) {
        return;
    }

    var stored = localStorage.getItem("hidePasswordAp");
    var hide;

    if (stored === null) {
        // primeira vez: começa escondido
        hide = true;
    } else {
        hide = (stored === "true");
    }

    checkbox.checked = hide;
    aplicarVisibilidadeSenha(hide);
});
