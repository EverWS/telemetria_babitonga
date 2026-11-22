# =============================================================================
# Build do firmware Datalogger — RP2040 da LilyGO T-PicoC3 (core rp2040 / arduino-pico).
#   make compile                 compila para a pasta build/ (artefato local)
#   make upload PORT=COMx        compila e grava na placa
#   make monitor PORT=COMx       abre o monitor serial
# [AJUSTE] PORT: porta COM da placa no seu PC (Gerenciador de Dispositivos).
# [AJUSTE] FQBN só muda se a placa mudar (arduino-cli board listall).
# =============================================================================
FQBN:=rp2040:rp2040:rpipico
PORT:=COM8
# Velocidade do monitor serial (igual ao Serial.begin do firmware).
BAUD:=115200

compile:
	arduino-cli compile -b ${FQBN} --build-path build

upload: compile
	arduino-cli upload -p ${PORT} -b ${FQBN} --input-dir build

monitor:
	arduino-cli monitor -p ${PORT} -b ${FQBN} -c ${BAUD}

# Apaga a pasta build/ (sintaxe do cmd do Windows).
clean:
	rmdir /s /q build
