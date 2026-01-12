# Manual flash helper - generates package, you flash manually
# Use this if adafruit-nrfutil has Python conflicts

$env:ZEPHYR_BASE = "C:\ncs\v3.2.1\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\ncs\toolchains\66cdf9b75e\opt\zephyr-sdk"

Write-Host "Manual flash helper for VapeLogger" -ForegroundColor Cyan

# Check if hex file exists
$hexFile = "build\usb_mic_xiao\zephyr\zephyr.hex"
if (-Not (Test-Path $hexFile)) {
    Write-Host "`nError: $hexFile not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

Write-Host "Found hex file: $hexFile" -ForegroundColor Green

# Try using nrfutil from ncs toolchain
$ncsNrfutil = "C:\ncs\toolchains\66cdf9b75e\opt\bin\Scripts\adafruit-nrfutil.exe"

if (Test-Path $ncsNrfutil) {
    Write-Host "Using nrfutil from ncs toolchain" -ForegroundColor Yellow

    Write-Host "`nMake sure the XIAO board is in bootloader mode (double-tap reset)" -ForegroundColor Yellow
    Write-Host "Press Enter when ready..." -ForegroundColor Yellow
    Read-Host

    $packageFile = "build\usb_mic_xiao\zephyr\vapelogger.zip"

    Write-Host "Creating DFU package..." -ForegroundColor Cyan
    & $ncsNrfutil dfu genpkg --dev-type 0x0052 --application $hexFile $packageFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nFailed to create DFU package" -ForegroundColor Red
        exit $LASTEXITCODE
    }

    Write-Host "Package created: $packageFile" -ForegroundColor Green

    # Try to find COM port
    Write-Host "`nSearching for bootloader COM port..." -ForegroundColor Cyan
    $comPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match "USB Serial Device" } | Select-Object -First 1

    if ($comPort) {
        $portName = $comPort.DeviceID
        Write-Host "Found bootloader at $portName" -ForegroundColor Green
        Write-Host "`nFlashing..." -ForegroundColor Cyan
        & $ncsNrfutil dfu serial --package $packageFile --port $portName -b 115200

        if ($LASTEXITCODE -eq 0) {
            Write-Host "`nFlashing complete!" -ForegroundColor Green
        }
    } else {
        Write-Host "`nNo COM port found. Please specify manually:" -ForegroundColor Yellow
        Write-Host "$ncsNrfutil dfu serial --package $packageFile --port COMX -b 115200" -ForegroundColor Cyan
    }
} else {
    Write-Host "`nCould not find nrfutil. Here are your options:" -ForegroundColor Yellow
    Write-Host "1. Copy $hexFile to the XIAO-SENSE drive (if UF2 bootloader works)" -ForegroundColor Cyan
    Write-Host "2. Use Arduino IDE or nRF Connect Programmer to flash $hexFile" -ForegroundColor Cyan
    Write-Host "3. Install adafruit-nrfutil in a clean Python environment" -ForegroundColor Cyan
}
