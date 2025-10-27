# tools/codex_cli.py
# Requisitos: pip install openai==2.*  (usar o Python do .venv)
import os
import sys
import argparse
import pathlib
import fnmatch
import textwrap

# ---- Modelo/credenciais por env var
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")  # obrigatório
OPENAI_MODEL   = os.getenv("OPENAI_MODEL", "gpt-5")  # deixe "gpt-5" se você tiver acesso

if not OPENAI_API_KEY:
    print("ERRO: defina OPENAI_API_KEY no ambiente do External Tool.", file=sys.stderr)
    sys.exit(2)

# Cliente (SDK 2.x)
from openai import OpenAI
client = OpenAI(api_key=OPENAI_API_KEY)

# -------- util --------
EXCLUDE_DIRS = {
    ".git", "build", "cmake-build-debug", "cmake-build-release",
    ".vscode", ".idea", "__pycache__", "managed_components",
    "components/esp_modbus/build",
}

# extensões típicas do seu projeto
INCLUDE_GLOBS = [
    "*.c","*.h","*.cpp","*.hpp","*.S",
    "*.cmake","CMakeLists.txt",
    "*.py","*.js","*.ts",
    "*.html","*.css",
    "*.ini","*.conf","*.yaml","*.yml","*.json",
    "*.ld","*.txt",
]

# Limites de segurança p/ tokens (ajuste se quiser)
MAX_FILES     = 450            # não mandar a árvore inteira em projetos gigantes
MAX_CHARS_ALL = 220_000        # ~150k tokens de contexto já é bastante
MAX_PER_FILE  = 6_000          # trunca arquivo grande (topo+fundo)

BANNER = "="*78


def should_include(path: pathlib.Path) -> bool:
    name = path.name
    for pat in INCLUDE_GLOBS:
        if fnmatch.fnmatch(name, pat):
            return True
    return False


def walk_repo(root: pathlib.Path):
    count = 0
    for p in root.rglob("*"):
        if p.is_dir():
            # Se QUALQUER parte do caminho está na lista de exclusão, ignore
            if any(part in EXCLUDE_DIRS for part in p.relative_to(root).parts):
                pass
            continue
        if any(part in EXCLUDE_DIRS for part in p.relative_to(root).parts):
            continue
        if should_include(p):
            yield p
            count += 1
            if count >= MAX_FILES:
                break


def smart_read(path: pathlib.Path) -> str:
    try:
        data = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""
    if len(data) <= MAX_PER_FILE:
        return data
    # mantém começo e fim
    head = data[: MAX_PER_FILE // 2]
    tail = data[- MAX_PER_FILE // 2 :]
    return head + "\n\n/* ...[TRUNCATED]... */\n\n" + tail


def build_corpus(root: pathlib.Path) -> str:
    pieces = []
    total = 0
    for f in walk_repo(root):
        content = smart_read(f)
        if not content:
            continue
        block = f"\n{BANNER}\n// FILE: {f.relative_to(root)}\n{BANNER}\n{content}\n"
        if total + len(block) > MAX_CHARS_ALL:
            pieces.append("\n/* --- CORPUS TRUNCATED BY LIMITS --- */\n")
            break
        pieces.append(block)
        total += len(block)
    return "".join(pieces)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".", help="raiz do projeto")
    ap.add_argument("--ask", default=None,
                    help="pergunta/objetivo (se vazio, lê de CODING_PROMPT.txt)")
    args = ap.parse_args()

    root = pathlib.Path(args.repo).resolve()
    if not root.exists():
        print(f"ERRO: repo não encontrado: {root}", file=sys.stderr)
        sys.exit(2)

    # prompt base: especializado em C/ESP-IDF sem “mexer no que funciona”
    system_prompt = textwrap.dedent("""
        Você é um engenheiro sênior de firmware (ESP-IDF/FreeRTOS, ESP32 + 4G/Wi-Fi).
        Objetivo: responder com trechos **mínimos e precisos** para melhorar/ajustar o código,
        SEM refatorar o que já está ok. Mostre:
        - Arquivo(s) e função(ões) afetados
        - Bloco “ANTES” e “DEPOIS” apenas onde precisa colar
        - Motivo (1–2 linhas)
        Evite mudanças cosméticas. Mantenha compatibilidade com ESP-IDF v5.3.x e os nomes já existentes.
    """).strip()

    user_goal = args.ask
    if not user_goal:
        prompt_file = root / "CODING_PROMPT.txt"
        if prompt_file.exists():
            user_goal = prompt_file.read_text(encoding="utf-8").strip()
        else:
            print('ERRO: use --ask "sua pergunta" ou crie CODING_PROMPT.txt na raiz.',
                  file=sys.stderr)
            sys.exit(2)

    corpus = build_corpus(root)

    user_message = (
        f"{user_goal}\n\n"
        f"{BANNER}\nPROJETO (recorte):\n{corpus}\n"
        f"{BANNER}\nFIM DO RECORTE\n"
    )

    # Escolhe o modelo (permite sobrescrever via variável de ambiente)
    model = os.getenv("OPENAI_MODEL", OPENAI_MODEL)

    # Monta os argumentos compatíveis com o modelo escolhido
    chat_kwargs = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user",   "content": user_message},
        ],
    }
    # Modelos tipo "gpt-5" não aceitam temperature/top_p/penalties != default
    if not model.lower().startswith("gpt-5"):
        chat_kwargs["temperature"] = 0.1  # ok para gpt-4.* / gpt-4o / etc.

    try:
        resp = client.chat.completions.create(**chat_kwargs)
    except Exception as e:
        print("FALHA na chamada à API:", repr(e), file=sys.stderr)
        sys.exit(3)

    content = resp.choices[0].message.content if resp.choices else ""
    print("\n===== RESPOSTA =====\n")
    print(content or "(vazio)")
    # opcional: também salvar em arquivo para consulta
    try:
        (root / "CODEX_RESPONSE.md").write_text(content, encoding="utf-8")
    except Exception:
        pass


if __name__ == "__main__":
    main()
