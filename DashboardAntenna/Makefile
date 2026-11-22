# =============================================================================
# Build do firmware DashboardAntenna — ESP32-C3 da LilyGO T-PicoC3 (core esp32).
#   make compile                 compila para a pasta build/ (artefato local)
#   make upload PORT=COMx        compila e grava na placa
#   make monitor PORT=COMx       abre o monitor serial
# [AJUSTE] PORT: porta COM da placa no seu PC (Gerenciador de Dispositivos).
# [AJUSTE] FQBN só muda se a placa mudar (arduino-cli board listall).
# =============================================================================
FQBN:=esp32:esp32:esp32c3
PORT:=COM11
# Velocidade do monitor serial (igual ao Serial.begin do firmware).
BAUD:=115200

compile:
	arduino-cli compile -b ${FQBN} --build-path build

upload: compile
	arduino-cli upload -p ${PORT} -b ${FQBN} --input-dir build

monitor:
	arduino-cli monitor -p ${PORT} -b ${FQBN} -c ${BAUD}
