# Build script for VapeLogger firmware
# Sets Zephyr environment variables and builds the application

$env:ZEPHYR_BASE = "C:\ncs\v3.2.1\zephyr"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\ncs\toolchains\66cdf9b75e\opt\zephyr-sdk"

Write-Host "Building VapeLogger firmware..." -ForegroundColor Cyan
west build -b xiao_ble/nrf52840/sense --pristine

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild successful!" -ForegroundColor Green
    Write-Host "Output: build/usb_mic_xiao/zephyr/zephyr.uf2" -ForegroundColor Yellow
} else {
    Write-Host "`nBuild failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
