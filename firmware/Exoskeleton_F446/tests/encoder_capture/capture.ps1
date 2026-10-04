param(
    [ValidateSet('prepare','capture')][string]$Mode='prepare',
    [switch]$PoweredWindowConfirmed,
    [string]$EvidenceDirectory,
    [string]$PythonExecutable='H:\app\python.exe',
    [string]$SerialModuleDirectory
)
$ErrorActionPreference='Stop'
if ($Mode -eq 'capture' -and -not $PoweredWindowConfirmed) { throw 'Capture requires main-thread confirmation of the explicitly powered window.' }
if (-not $EvidenceDirectory) { $EvidenceDirectory=Join-Path $PSScriptRoot ('evidence-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')) }
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
$toolRoot=[IO.Path]::GetFullPath($PSScriptRoot)
if (-not $EvidenceDirectory.StartsWith($toolRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence directory must be under capture_tool.' }
if (Test-Path -LiteralPath $EvidenceDirectory) { throw 'Use a new evidence directory; never resume an old capture.' }
New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
$ports=@(Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -like 'USB\VID_0483&PID_374B&MI_02\*' } | Select-Object DeviceID,Name,PNPDeviceID,Status,ConfigManagerErrorCode)
if ($ports.Count -ne 1 -or $ports[0].DeviceID -notmatch '^COM\d+$' -or $ports[0].ConfigManagerErrorCode -ne 0 -or $ports[0].Status -ne 'OK') { throw 'No unique healthy ST-LINK VCP.' }
$parent=(Get-PnpDeviceProperty -InstanceId $ports[0].PNPDeviceID -KeyName 'DEVPKEY_Device_Parent').Data
if ($parent -cne 'USB\VID_0483&PID_374B\066EFF555748874987182240') { throw 'Wrong ST-LINK serial parent.' }
$binding=[ordered]@{at_utc=[DateTime]::UtcNow.ToString('o');ports=$ports;parent=$parent;serial_verified=$true;read_only_enumeration=$true}
$binding | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'binding.json') -Encoding utf8
# Recheck immediately before opening; never reset or toggle DTR/RTS.
$live=@(Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -like 'USB\VID_0483&PID_374B&MI_02\*' })
if ($live.Count -ne 1 -or $live[0].DeviceID -ne $ports[0].DeviceID -or $live[0].PNPDeviceID -ne $ports[0].PNPDeviceID -or $live[0].ConfigManagerErrorCode -ne 0 -or $live[0].Status -ne 'OK') { throw 'VCP mapping changed.' }
if ((Get-PnpDeviceProperty -InstanceId $live[0].PNPDeviceID -KeyName 'DEVPKEY_Device_Parent').Data -cne $parent) { throw 'VCP serial parent changed.' }
$pythonArgs=@('-B','-u',(Join-Path $PSScriptRoot 'capture.py'),'--mode',$Mode,'--binding',(Join-Path $EvidenceDirectory 'binding.json'),'--evidence',$EvidenceDirectory)
if ($PoweredWindowConfirmed) { $pythonArgs += '--powered-window-confirmed' }
if ($SerialModuleDirectory) { $pythonArgs += @('--serial-module-directory',$SerialModuleDirectory) }
if (-not (Test-Path -LiteralPath $PythonExecutable) -and $PythonExecutable -eq 'H:\app\python.exe') { $PythonExecutable='python' }
& $PythonExecutable @pythonArgs
exit $LASTEXITCODE
