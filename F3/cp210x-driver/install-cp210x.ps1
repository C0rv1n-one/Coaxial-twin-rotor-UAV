$ErrorActionPreference = 'Continue'
$log = 'C:\Users\y9000p\Desktop\workspace\cp210x-driver\install.log'
$inf = 'C:\Users\y9000p\Desktop\workspace\cp210x-driver\extracted\silabser.inf'

"=== START $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Out-File -FilePath $log -Encoding UTF8
"Admin: $(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))" | Out-File -FilePath $log -Append -Encoding UTF8

"--- add-driver /install ---" | Out-File -FilePath $log -Append -Encoding UTF8
& pnputil.exe /add-driver "$inf" /install *>&1 | Out-File -FilePath $log -Append -Encoding UTF8
"exit=$LASTEXITCODE" | Out-File -FilePath $log -Append -Encoding UTF8

"--- scan-devices ---" | Out-File -FilePath $log -Append -Encoding UTF8
& pnputil.exe /scan-devices *>&1 | Out-File -FilePath $log -Append -Encoding UTF8
"exit=$LASTEXITCODE" | Out-File -FilePath $log -Append -Encoding UTF8

Start-Sleep -Seconds 4

"--- device state ---" | Out-File -FilePath $log -Append -Encoding UTF8
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_10C4' } | ForEach-Object {
    "Name=$($_.FriendlyName) | Status=$($_.Status) | Class=$($_.Class) | Problem=$($_.Problem) | Inst=$($_.InstanceId)"
} | Out-File -FilePath $log -Append -Encoding UTF8

"--- COM ports ---" | Out-File -FilePath $log -Append -Encoding UTF8
Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | ForEach-Object {
    "Name=$($_.FriendlyName) | Status=$($_.Status) | Inst=$($_.InstanceId)"
} | Out-File -FilePath $log -Append -Encoding UTF8

"=== END $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Out-File -FilePath $log -Append -Encoding UTF8
