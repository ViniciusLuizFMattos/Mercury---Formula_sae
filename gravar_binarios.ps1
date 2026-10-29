# Grava os binários prontos (modo demo) na placa, sem precisar compilar.
# Uso:  .\gravar_binarios.ps1 -Porta COM8
# SEGURE o botão BOOT da placa enquanto o script tenta conectar e solte quando aparecer "Writing".
param([string]$Porta = "COM8")
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$b = Join-Path $aqui "binarios_prontos"
$esp = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" -Recurse -Filter "esptool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $esp) { Write-Host "esptool não encontrado. Instale a plataforma ESP32 no Arduino IDE ou use: pip install esptool"; exit 1 }
& $esp.FullName --chip esp32 --port $Porta --baud 921600 --connect-attempts 120 --before default_reset --after no_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB `
  0x1000 "$b\painel.ino.bootloader.bin" 0x8000 "$b\painel.ino.partitions.bin" 0xe000 "$b\boot_app0.bin" 0x10000 "$b\painel.ino.bin"
