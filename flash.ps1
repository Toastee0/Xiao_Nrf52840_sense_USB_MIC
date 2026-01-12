# Flash script for VapeLogger firmware
# Uses adafruit-nrfutil to flash via DFU

$env:ZEPHYR_BASE = "C:\ncs\v3.2.1\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\ncs\toolchains\66cdf9b75e\opt\zephyr-sdk"

Write-Host "Flashing VapeLogger firmware via DFU..." -ForegroundColor Cyan
Write-Host "Make sure the XIAO board is in bootloader mode (double-tap reset)" -ForegroundColor Yellow

# Check if hex file exists
$hexFile = "build\usb_mic_xiao\zephyr\zephyr.hex"
if (-Not (Test-Path $hexFile)) {
    Write-Host "`nError: $hexFile not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

# Check if adafruit-nrfutil is installed
$nrfutil = Get-Command adafruit-nrfutil -ErrorAction SilentlyContinue
if (-Not $nrfutil) {
    Write-Host "`nError: adafruit-nrfutil not found in PATH" -ForegroundColor Red
    Write-Host "Install with: pip install adafruit-nrfutil" -ForegroundColor Yellow
    exit 1
}

# Create DFU package
Write-Host "`nCreating DFU package..." -ForegroundColor Cyan
$packageFile = "build\usb_mic_xiao\zephyr\vapelogger.zip"
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application $hexFile $packageFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nFailed to create DFU package" -ForegroundColor Red
    exit $LASTEXITCODE
}

# Find COM port
Write-Host "`nSearching for XIAO bootloader COM port..." -ForegroundColor Cyan
$comPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match "USB Serial Device" -or $_.Description -match "Feather" } | Select-Object -First 1

if ($comPort) {
    $portName = $comPort.DeviceID
    Write-Host "Found bootloader at $portName" -ForegroundColor Green

    # Flash via DFU
    Write-Host "`nFlashing firmware..." -ForegroundColor Cyan
    adafruit-nrfutil dfu serial --package $packageFile --port $portName -b 115200

    if ($LASTEXITCODE -eq 0) {
        Write-Host "`nFlashing complete! Board will reset automatically." -ForegroundColor Green
    } else {
        Write-Host "`nFlashing failed" -ForegroundColor Red
        exit $LASTEXITCODE
    }
} else {
    Write-Host "`nCOM port not found automatically." -ForegroundColor Yellow
    Write-Host "Please manually specify the port:" -ForegroundColor Yellow
    Write-Host "adafruit-nrfutil dfu serial --package $packageFile --port COMX -b 115200" -ForegroundColor Cyan
    Write-Host "`nAvailable COM ports:" -ForegroundColor Yellow
    Get-WmiObject Win32_SerialPort | Format-Table DeviceID, Description
}
