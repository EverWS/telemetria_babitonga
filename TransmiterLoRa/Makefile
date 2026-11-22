# =============================================================================
# Build do firmware TransmiterLoRa — Heltec WiFi LoRa 32 V2 do barco (core Heltec-esp32).
#   make compile                 compila para a pasta build/ (artefato local)
#   make upload PORT=COMx        compila e grava na placa
#   make monitor PORT=COMx       abre o monitor serial
# [AJUSTE] PORT: porta COM da placa no seu PC (Gerenciador de Dispositivos).
# [AJUSTE] FQBN só muda se a placa mudar (arduino-cli board listall).
# =============================================================================
FQBN:=Heltec-esp32:esp32:heltec_wifi_lora_32_V2
PORT:=/dev/ttyUSB0
# Velocidade do monitor serial (igual ao Serial.begin do firmware).
BAUD:=115200

compile:
	arduino-cli compile -b ${FQBN} --build-path build

upload: compile
	arduino-cli upload -p ${PORT} -b ${FQBN} --input-dir build

monitor:
	arduino-cli monitor -p ${PORT} -b ${FQBN} -c ${BAUD}
