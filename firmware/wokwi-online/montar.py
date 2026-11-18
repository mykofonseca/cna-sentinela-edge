"""Monta as pastas z1/ e z2/ com os arquivos para colar no projeto online do Wokwi.

O wokwi.com não usa a estrutura do PlatformIO: cada arquivo vira uma aba solta e o
programa principal precisa se chamar sketch.ino. A fonte continua sendo
firmware/sentinela; rode este script sempre que o firmware mudar:

    python firmware/wokwi-online/montar.py
"""

import re
import shutil
from pathlib import Path

AQUI = Path(__file__).resolve().parent
FONTE = AQUI.parent / "sentinela"

ARQUIVOS = [
    ("src/main.cpp", "sketch.ino"),
    ("include/config.h", "config.h"),
    ("include/maquina_estados.h", "maquina_estados.h"),
    ("src/maquina_estados.cpp", "maquina_estados.cpp"),
    ("include/rede.h", "rede.h"),
    ("src/rede.cpp", "rede.cpp"),
    ("include/certificados.h", "certificados.h"),
    ("include/secrets.example.h", "secrets.example.h"),
    ("diagram.json", "diagram.json"),
]

LIBRARIES = """# Bibliotecas do projeto online (aba libraries.txt do Wokwi)
PubSubClient
DHT sensor library
Adafruit Unified Sensor
LiquidCrystal I2C
"""


def montar(zona):
    destino = AQUI / f"z{zona}"
    if destino.exists():
        shutil.rmtree(destino)
    destino.mkdir()
    for origem, nome in ARQUIVOS:
        texto = (FONTE / origem).read_text(encoding="utf-8")
        if nome == "config.h":
            # No Wokwi online não há build_flags: a zona fica fixa no config.h.
            texto, n = re.subn(r"#define ZONA_ID \d+", f"#define ZONA_ID {zona}", texto)
            assert n == 1, "ZONA_ID não encontrado em config.h"
        (destino / nome).write_text(texto, encoding="utf-8", newline="\n")
    (destino / "libraries.txt").write_text(LIBRARIES, encoding="utf-8", newline="\n")
    print(f"z{zona}: {len(ARQUIVOS) + 1} arquivos em {destino}")


if __name__ == "__main__":
    for z in (1, 2):
        montar(z)
