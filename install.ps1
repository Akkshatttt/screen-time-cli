# Installs screen-time-cli for the current user (no admin): downloads the latest release and runs its installer.
#   irm https://raw.githubusercontent.com/Akkshatttt/screen-time-cli/main/install.ps1 | iex
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Windows PowerShell 5.1 downloads very slowly with the progress bar
$d = Join-Path $env:TEMP 'stc'
iwr https://github.com/Akkshatttt/screen-time-cli/releases/latest/download/screen-time-cli-win32.zip -OutFile "$d.zip"
Expand-Archive "$d.zip" $d -Force
gci $d | Unblock-File
& "$d\screen-time-cli.exe" install
