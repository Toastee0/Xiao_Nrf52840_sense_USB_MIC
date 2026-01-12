# Flash script for VapeLogger - uses full path to adafruit-nrfutil
# Avoids Python DLL conflicts

$env:ZEPHYR_BASE = "C:\ncs\v3.2.1\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\ncs\toolchains\66cdf9b75e\opt\zephyr-sdk"

Write-Host "Flashing VapeLogger firmware via DFU..." -ForegroundColor Cyan
Write-Host "Make sure the XIAO board is in bootloader mode (double-tap reset)" -ForegroundColor Yellow
Write-Host ""

# Check if hex file exists
$hexFile = "build\usb_mic_xiao\zephyr\zephyr.hex"
if (-Not (Test-Path $hexFile)) {
    Write-Host "Error: $hexFile not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

Write-Host "Found hex file: $hexFile" -ForegroundColor Green

# Use full path to adafruit-nrfutil
$nrfutil = "C:\Python313\Scripts\adafruit-nrfutil.exe"

if (-Not (Test-Path $nrfutil)) {
    Write-Host "Error: adafruit-nrfutil not found at $nrfutil" -ForegroundColor Red
    exit 1
}

# Temporarily clear Python path to avoid DLL conflicts
$oldPath = $env:PATH
$env:PATH = "C:\Python313;C:\Python313\Scripts;C:\Windows\System32"

try {
    # Create DFU package
    Write-Host "`nCreating DFU package..." -ForegroundColor Cyan
    $packageFile = "build\usb_mic_xiao\zephyr\vapelogger.zip"

    & $nrfutil dfu genpkg --dev-type 0x0052 --application $hexFile $packageFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nFailed to create DFU package" -ForegroundColor Red
        exit $LASTEXITCODE
    }

    Write-Host "Package created successfully" -ForegroundColor Green

    # Find COM port
    Write-Host "`nSearching for XIAO bootloader COM port..." -ForegroundColor Cyan
    $comPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match "USB Serial Device" } | Select-Object -First 1

    if ($comPort) {
        $portName = $comPort.DeviceID
        Write-Host "Found bootloader at $portName" -ForegroundColor Green

        # Flash via DFU
        Write-Host "`nFlashing firmware..." -ForegroundColor Cyan
        & $nrfutil dfu serial --package $packageFile --port $portName -b 115200

        if ($LASTEXITCODE -eq 0) {
            Write-Host "`nFlashing complete! Board will reset automatically." -ForegroundColor Green
            Write-Host "USB audio device should enumerate shortly" -ForegroundColor Yellow
            Write-Host "Serial console at 115200 baud will show COIL readings" -ForegroundColor Yellow
        } else {
            Write-Host "`nFlashing failed with exit code $LASTEXITCODE" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    } else {
        Write-Host "`nCOM port not found automatically." -ForegroundColor Yellow
        Write-Host "Please manually specify the port:" -ForegroundColor Yellow
        Write-Host "$nrfutil dfu serial --package $packageFile --port COMX -b 115200" -ForegroundColor Cyan
        Write-Host "`nAvailable COM ports:" -ForegroundColor Yellow
        Get-WmiObject Win32_SerialPort | Format-Table DeviceID, Description
    }
} finally {
    # Restore original PATH
    $env:PATH = $oldPath
}
