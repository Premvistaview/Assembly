# Select a level and a floorplan JSON, run Compute Assembly, place furniture, and package a Windows EXE.

$ErrorActionPreference = 'Stop'

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne 'STA') {
    & powershell -NoProfile -STA -ExecutionPolicy Bypass -File $PSCommandPath @args
    exit $LASTEXITCODE
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ProjectFile = Join-Path $ProjectRoot 'Assembly.uproject'
$ContentRoot = Join-Path $ProjectRoot 'Content'
$IniPath = Join-Path $ProjectRoot 'Config\DefaultEngine.ini'
$IniBackup = Join-Path $ProjectRoot 'Saved\PlaceAssembly\DefaultEngine.ini.bak'
$BuildReport = Join-Path $ProjectRoot 'Saved\PlaceAssembly\build.txt'
$PackageRoot = Join-Path $ProjectRoot 'Packaged'
$GameMode = '/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C'

function Find-UnrealEngine {
    $Candidates = @(
        'E:\UE_5.7',
        'C:\Program Files\Epic Games\UE_5.7',
        'D:\UE_5.7'
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path (Join-Path $Candidate 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) {
            return $Candidate
        }
    }
    foreach ($Key in @(
        'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.7',
        'HKCU:\SOFTWARE\EpicGames\Unreal Engine\5.7'
    )) {
        if (Test-Path $Key) {
            $Installed = (Get-ItemProperty $Key).InstalledDirectory
            if ($Installed -and (Test-Path (Join-Path $Installed 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'))) {
                return $Installed
            }
        }
    }
    throw 'Unreal Engine 5.7 was not found. Install it or edit Find-UnrealEngine in Scripts\PlaceAssembly.ps1.'
}

function Get-ProjectLevels {
    Get-ChildItem -Path $ContentRoot -Filter '*.umap' -Recurse -File |
        Where-Object { $_.FullName -notmatch '\\__External|\\_GENERATED\\' } |
        ForEach-Object {
            $Relative = $_.FullName.Substring($ContentRoot.Length + 1).Replace('\', '/')
            $AssetPath = [System.IO.Path]::ChangeExtension($Relative, $null).TrimEnd('.')
            [PSCustomObject]@{
                Label = $AssetPath
                Package = "/Game/$AssetPath"
            }
        } |
        Sort-Object Label -Unique
}

function Select-PackageLevel {
    param([System.Collections.IEnumerable]$Levels)

    if ($Levels.Count -eq 0) {
        throw "No .umap levels were found under $ContentRoot."
    }

    Write-Host ''
    Write-Host 'Which level should the packaged exe open?'
    for ($Index = 0; $Index -lt $Levels.Count; $Index++) {
        Write-Host ("  [{0}] {1}" -f ($Index + 1), $Levels[$Index].Label)
    }
    Write-Host ''

    while ($true) {
        $UserInput = Read-Host 'Enter number'
        if ($UserInput -match '^\d+$') {
            $Choice = [int]$UserInput
            if ($Choice -ge 1 -and $Choice -le $Levels.Count) {
                return $Levels[$Choice - 1]
            }
        }
        Write-Host 'Enter a number from the list.'
    }
}

function Select-JsonFile {
    param([string[]]$ScriptArgs)

    if ($ScriptArgs.Count -gt 0 -and (Test-Path -LiteralPath $ScriptArgs[0])) {
        return (Resolve-Path -LiteralPath $ScriptArgs[0]).Path
    }

    Add-Type -AssemblyName System.Windows.Forms
    $Dialog = New-Object System.Windows.Forms.OpenFileDialog
    $Dialog.Title = 'Select floorplan JSON (Compute Assembly)'
    $Dialog.Filter = 'Floorplan JSON (*.json)|*.json'
    $Dialog.InitialDirectory = Join-Path $ProjectRoot 'Content\Furniture\Detections'
    $Dialog.Multiselect = $false
    if ($Dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        return $null
    }
    return $Dialog.FileName
}

function Read-BuildValue([string]$Name) {
    foreach ($Line in Get-Content -LiteralPath $BuildReport) {
        if ($Line.StartsWith("$Name=")) {
            return $Line.Substring($Name.Length + 1)
        }
    }
    throw "The build report is missing $Name."
}

$Levels = @(Get-ProjectLevels)
$SelectedLevel = Select-PackageLevel $Levels

$JsonPath = Select-JsonFile @args
if ([string]::IsNullOrWhiteSpace($JsonPath)) {
    Write-Host 'No JSON file selected.'
    exit 1
}

$EngineRoot = Find-UnrealEngine
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$RunUAT = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'

Write-Host ''
Write-Host "Level: $($SelectedLevel.Package)"
Write-Host "JSON:  $JsonPath"
Write-Host 'Compiling the editor tools...'
cmd.exe /c "`"$BuildBat`" AssemblyEditor Win64 Development -Project=`"$ProjectFile`" -WaitMutex"
if ($LASTEXITCODE -ne 0) {
    throw "Editor compile failed with exit code $LASTEXITCODE."
}

Write-Host 'Opening the level and placing furniture from Compute Assembly...'
cmd.exe /c "`"$Editor`" `"$ProjectFile`" -run=PlaceAssembly -Json=`"$JsonPath`" -Map=$($SelectedLevel.Package) -unattended -nosplash -nopause -stdout -FullStdOutLogOutput"
if ($LASTEXITCODE -ne 0) {
    throw "Place Assembly failed with exit code $LASTEXITCODE. See Saved\Logs."
}
if (-not (Test-Path -LiteralPath $BuildReport)) {
    throw "Place Assembly finished without writing $BuildReport."
}

$MapPackage = Read-BuildValue 'MapPackage'
$MapObject = Read-BuildValue 'MapObject'
Write-Host "Saved: $MapObject"
Write-Host 'Packaging the Windows EXE. This cooks the project and can take a long time.'

$IniChanged = $false
try {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $IniBackup) | Out-Null
    Copy-Item -LiteralPath $IniPath -Destination $IniBackup -Force
    $IniText = Get-Content -LiteralPath $IniPath -Raw
    $IniText = [regex]::Replace($IniText, '(?m)^GameDefaultMap=.*$', "GameDefaultMap=$MapObject")
    $IniText = [regex]::Replace($IniText, '(?m)^GlobalDefaultGameMode=.*$', "GlobalDefaultGameMode=$GameMode")
    $Utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($IniPath, $IniText, $Utf8)
    $IniChanged = $true

    New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
    cmd.exe /c "`"$RunUAT`" BuildCookRun -project=`"$ProjectFile`" -noP4 -platform=Win64 -clientconfig=Development -cook -build -stage -pak -archive -archivedirectory=`"$PackageRoot`" -map=$MapPackage -utf8output -unattended"
    if ($LASTEXITCODE -ne 0) {
        throw "Packaging failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ($IniChanged -and (Test-Path -LiteralPath $IniBackup)) {
        Copy-Item -LiteralPath $IniBackup -Destination $IniPath -Force
    }
}

$Executables = Get-ChildItem -LiteralPath $PackageRoot -Filter 'Assembly.exe' -Recurse -ErrorAction SilentlyContinue
if ($Executables.Count -eq 0) {
    throw "Packaging finished, but Assembly.exe was not found under $PackageRoot."
}

Write-Host ''
Write-Host 'Packaged executable:'
foreach ($Executable in $Executables) {
    Write-Host $Executable.FullName
}
