$ErrorActionPreference = 'Stop'
$out = 'C:\Users\y9000p\Desktop\workspace\spracingf3-cli-dump-3.3.3.txt'
$port = New-Object System.IO.Ports.SerialPort('COM7', 115200, 'None', 8, 'One')
$port.ReadTimeout = 800
$port.WriteTimeout = 800
try { $port.Open() } catch {
    Write-Output "OPENFAIL: $($_.Exception.Message)"
    exit 2
}
Start-Sleep -Milliseconds 400
try { $port.DiscardInBuffer() } catch {}

$sb = New-Object System.Text.StringBuilder
function Drain([int]$ms) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        try {
            $d = $port.ReadExisting()
            if ($d) { [void]$sb.Append($d) }
        } catch { Start-Sleep -Milliseconds 60 }
        Start-Sleep -Milliseconds 40
    }
}

$port.Write("#`r`n")
Drain 1200
$port.Write("version`r`n")
Drain 800
$port.Write("dump`r`n")
Drain 14000
$port.Write("exit`r`n")
Drain 1200
try { $port.Close() } catch {}

$txt = $sb.ToString()
[System.IO.File]::WriteAllText($out, $txt, [System.Text.Encoding]::UTF8)
Write-Output ("LEN=" + $txt.Length)
Write-Output ("SAVED=" + $out)
Write-Output ("HEAD=" + ($txt.Substring(0, [Math]::Min(200, $txt.Length)) -replace "`r`n", ' | '))
