param(
    [switch]$Live,
    [switch]$PoweredWindowConfirmed,
    [switch]$Demo,
    [string]$PythonExecutable='H:\app\python.exe'
)
$ErrorActionPreference='Stop'
if ($Live -and $Demo) { throw 'Demo and Live are mutually exclusive.' }
if ([bool]$Live -ne [bool]$PoweredWindowConfirmed) { throw 'Live requires both -Live and -PoweredWindowConfirmed.' }
$uiArgs=@('-B','-u','-X','utf8',(Join-Path $PSScriptRoot 'visible_capture.py'),'--python-executable',$PythonExecutable)
if ($Live) { $uiArgs += @('--live','--powered-window-confirmed') }
if ($Demo) { $uiArgs += '--demo' }
# Foreground launch is intentional: the user requested a visible host window.
# Default window has no device enumeration, serial access or capture process.
& $PythonExecutable @uiArgs
exit $LASTEXITCODE
