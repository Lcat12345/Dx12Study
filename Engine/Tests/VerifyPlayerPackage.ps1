param(
    [string]$PackageDir,
    [string]$PlayerOutputDir
)

$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $engineRoot

if ([string]::IsNullOrWhiteSpace($PackageDir))
{
    $PackageDir = Join-Path $repoRoot 'Output\x64\Release\PlayerPackage'
}
if ([string]::IsNullOrWhiteSpace($PlayerOutputDir))
{
    $PlayerOutputDir = Join-Path $repoRoot 'Output\x64\Release\Player'
}

$packagePath = [IO.Path]::GetFullPath($PackageDir).TrimEnd('\')
$playerOutputPath = [IO.Path]::GetFullPath($PlayerOutputDir).TrimEnd('\')
$playerExe = Join-Path $packagePath 'Player.exe'
$manifestPath = Join-Path $packagePath 'package-manifest.txt'

foreach ($required in @($playerExe, $manifestPath,
                         (Join-Path $packagePath 'Assets\Scenes\Arena.scene'),
                         (Join-Path $packagePath 'Assets\Scenes\Demo.scene'),
                         (Join-Path $packagePath 'Shaders\Basic.VS.cso')))
{
    if (-not (Test-Path -LiteralPath $required -PathType Leaf))
    {
        throw "Player package is incomplete: $required"
    }
}

$allowedTopLevel = @('Player.exe', 'Assets', 'Shaders', 'package-manifest.txt')
foreach ($item in Get-ChildItem -LiteralPath $packagePath -Force)
{
    if ($allowedTopLevel -notcontains $item.Name -and $item.Extension -ine '.dll')
    {
        throw "Unexpected top-level package item: $($item.Name)"
    }
}

$forbiddenExtensions = @('.slnx', '.vcxproj', '.cpp', '.c', '.h', '.hpp', '.hlsl')
foreach ($file in Get-ChildItem -LiteralPath $packagePath -File -Recurse -Force)
{
    if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant() -or
        $file.Name -ieq 'imgui.ini' -or
        $file.Name -imatch '^imgui.*\.(ini|cpp|c|h|hpp)$')
    {
        throw "Forbidden file in Player package: $($file.FullName)"
    }
}

$manifestLines = @(Get-Content -LiteralPath $manifestPath)
$fileMarker = [Array]::IndexOf($manifestLines, '[files]')
if ($fileMarker -lt 0)
{
    throw 'package-manifest.txt has no [files] section'
}
$metadata = $manifestLines[0..($fileMarker - 1)] -join "`n"
foreach ($requiredMetadata in @('configuration=Release', 'platform=x64',
                                'commit=', 'default_scene=Assets/Scenes/Arena.scene',
                                'runtime_dlls='))
{
    if ($metadata.IndexOf($requiredMetadata, [StringComparison]::Ordinal) -lt 0)
    {
        throw "package-manifest.txt is missing '$requiredMetadata'"
    }
}

$manifestFiles = @($manifestLines[($fileMarker + 1)..($manifestLines.Count - 1)] |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
$prefixLength = $packagePath.Length + 1
$actualFiles = @(Get-ChildItem -LiteralPath $packagePath -File -Recurse -Force |
    ForEach-Object {
        $_.FullName.Substring($prefixLength).Replace('\', '/')
    } | Sort-Object -Unique)
$manifestDifference = @(Compare-Object -ReferenceObject $manifestFiles -DifferenceObject $actualFiles)
if ($manifestDifference.Count -ne 0)
{
    throw "Package contents do not match package-manifest.txt:`n$($manifestDifference | Out-String)"
}

$mapPath = Join-Path $playerOutputPath 'Player.map'
if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf))
{
    throw "Missing Player link map for binary audit: $mapPath"
}
$mapText = Get-Content -LiteralPath $mapPath -Raw
foreach ($symbol in @('ImGui::', 'ImGui_Impl', 'DebugUI', 'AssetBrowser',
                      'EditorSession', 'EditorApp'))
{
    if ($mapText.IndexOf($symbol, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    {
        throw "Player.map contains forbidden Editor/ImGui symbol '$symbol'"
    }
}

if (-not ('PlayerPackageNativeMethods' -as [type]))
{
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class PlayerPackageNativeMethods
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(IntPtr hwnd, int x, int y,
                                         int width, int height, bool repaint);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hwnd, uint message,
                                          UIntPtr wParam, IntPtr lParam);
}
'@
}

function Invoke-IsolatedPlayer(
    [string]$Executable,
    [string]$WorkingDirectory,
    [string]$RuntimeLog,
    [string[]]$Arguments,
    [string]$ExpectedScene)
{
    $oldLogPath = [Environment]::GetEnvironmentVariable(
        'DX12ENGINE_RUNTIME_PATH_LOG', 'Process')
    [Environment]::SetEnvironmentVariable(
        'DX12ENGINE_RUNTIME_PATH_LOG', $RuntimeLog, 'Process')

    try
    {
        $start = @{
            FilePath = $Executable
            WorkingDirectory = $WorkingDirectory
            WindowStyle = 'Normal'
            PassThru = $true
        }
        if ($Arguments.Count -ne 0)
        {
            $start.ArgumentList = $Arguments
        }
        $process = Start-Process @start
    }
    finally
    {
        [Environment]::SetEnvironmentVariable(
            'DX12ENGINE_RUNTIME_PATH_LOG', $oldLogPath, 'Process')
    }

    try
    {
        $hwnd = [IntPtr]::Zero
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([DateTime]::UtcNow -lt $deadline)
        {
            $process.Refresh()
            if ($process.HasExited)
            {
                throw "Player exited before creating its window (exit $($process.ExitCode))"
            }
            $hwnd = $process.MainWindowHandle
            if ($hwnd -ne [IntPtr]::Zero -and $process.MainWindowTitle -like "*$ExpectedScene*")
            {
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if ($hwnd -eq [IntPtr]::Zero)
        {
            throw 'Timed out waiting for the isolated Player window'
        }

        # Exercise resize, demo interaction, and both MSAA presentation variants.
        [PlayerPackageNativeMethods]::MoveWindow($hwnd, 40, 40, 960, 540, $true) | Out-Null
        foreach ($key in @(69, 77, 77)) # E interaction; M toggles 4x -> 1x -> 4x.
        {
            [PlayerPackageNativeMethods]::PostMessage(
                $hwnd, 0x0100, [UIntPtr]::new([uint32]$key), [IntPtr]::Zero) | Out-Null
            [PlayerPackageNativeMethods]::PostMessage(
                $hwnd, 0x0101, [UIntPtr]::new([uint32]$key), [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 350
        }

        $process.Refresh()
        if ($process.HasExited)
        {
            throw "Player failed during resize/input/MSAA exercise (exit $($process.ExitCode))"
        }

        [PlayerPackageNativeMethods]::PostMessage(
            $hwnd, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (-not $process.WaitForExit(10000))
        {
            throw 'Player did not exit after WM_CLOSE'
        }
        if ($process.ExitCode -ne 0)
        {
            throw "Player returned non-zero exit code $($process.ExitCode)"
        }
    }
    finally
    {
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
        $process.Dispose()
    }

    if (-not (Test-Path -LiteralPath $RuntimeLog -PathType Leaf))
    {
        throw "Player did not write its runtime path log: $RuntimeLog"
    }
    return Get-Content -LiteralPath $RuntimeLog -Raw
}

function Invoke-InvalidScenePlayer(
    [string]$Executable,
    [string]$WorkingDirectory,
    [string]$ApplicationLogDir)
{
    $before = @(Get-ChildItem -LiteralPath $ApplicationLogDir -File `
        -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $process = Start-Process -FilePath $Executable `
        -WorkingDirectory $WorkingDirectory -WindowStyle Normal -PassThru `
        -ArgumentList @('--scene', 'Assets\Scenes\IntentionallyMissing.scene')

    try
    {
        $messageBox = [IntPtr]::Zero
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([DateTime]::UtcNow -lt $deadline)
        {
            $process.Refresh()
            if ($process.HasExited)
            {
                break
            }
            $messageBox = [PlayerPackageNativeMethods]::FindWindow(
                '#32770', 'Dx12Engine Player failed')
            if ($messageBox -ne [IntPtr]::Zero)
            {
                break
            }
            Start-Sleep -Milliseconds 100
        }
        $process.Refresh()
        if ($messageBox -eq [IntPtr]::Zero -and -not $process.HasExited)
        {
            throw 'Invalid-Scene Player neither exited nor displayed its error MessageBox'
        }

        if ($messageBox -ne [IntPtr]::Zero)
        {
            [PlayerPackageNativeMethods]::PostMessage(
                $messageBox, 0x0010, [UIntPtr]::Zero,
                [IntPtr]::Zero) | Out-Null
            if (-not $process.WaitForExit(10000))
            {
                throw 'Invalid-Scene Player did not exit after its MessageBox closed'
            }
        }
    }
    finally
    {
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
        $process.Dispose()
    }

    $created = @(Get-ChildItem -LiteralPath $ApplicationLogDir -File |
        Where-Object { $before -notcontains $_.FullName })
    if ($created.Count -ne 1)
    {
        throw "Invalid-Scene Player produced $($created.Count) application logs instead of one"
    }
    $text = Get-Content -LiteralPath $created[0].FullName -Raw
    if ($text.IndexOf('unhandled_exception', [StringComparison]::Ordinal) -lt 0 -or
        $text.IndexOf('IntentionallyMissing.scene', [StringComparison]::Ordinal) -lt 0)
    {
        throw 'Invalid-Scene Player application log omitted the reported error'
    }
}

function Remove-TemporaryRoot(
    [string]$Path,
    [int]$Attempts = 10,
    [int]$DelayMilliseconds = 200)
{
    # Windows can hold the Player working-directory handle briefly after the
    # process exits, so retry instead of failing an otherwise passing run.
    $lastError = $null
    for ($attempt = 1; $attempt -le $Attempts; $attempt++)
    {
        try
        {
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
        }
        catch
        {
            $lastError = $_
        }
        # A delete can also report success while the directory lingers, so the
        # path itself - not the absence of an error - decides when we are done.
        if (-not (Test-Path -LiteralPath $Path))
        {
            return
        }
        Start-Sleep -Milliseconds $DelayMilliseconds
    }

    $reason = if ($null -ne $lastError) { $lastError.Exception.Message }
              else { 'the directory still exists' }
    Write-Warning ("Could not remove the temporary verification directory " +
                   "'$Path' after $Attempts attempts: $reason")
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'Dx12Engine-Phase12.7-' + [Guid]::NewGuid().ToString('N'))
$isolatedPackage = Join-Path $tempRoot 'PlayerPackage'
$workingDirectory = Join-Path $tempRoot 'ExternalWorkingDirectory'

try
{
    New-Item -ItemType Directory -Path $isolatedPackage | Out-Null
    New-Item -ItemType Directory -Path $workingDirectory | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $packagePath -Force)
    {
        Copy-Item -LiteralPath $item.FullName -Destination $isolatedPackage -Recurse -Force
    }

    $isolatedExe = Join-Path $isolatedPackage 'Player.exe'
    $defaultLog = Join-Path $tempRoot 'default-runtime-paths.log'
    $demoLog = Join-Path $tempRoot 'demo-runtime-paths.log'
    $explicitLog = Join-Path $tempRoot 'explicit-runtime-paths.log'
    $applicationLogDir = Join-Path $tempRoot 'application-logs'
    $oldApplicationLogDir = [Environment]::GetEnvironmentVariable(
        'DX12ENGINE_LOG_DIR', 'Process')
    [Environment]::SetEnvironmentVariable(
        'DX12ENGINE_LOG_DIR', $applicationLogDir, 'Process')
    try
    {
        # No arguments is the shipped path and now opens the arena. Demo.scene
        # is run explicitly because it is still the widest content in the
        # package - transparency, normal maps, skybox, interaction - and it
        # stopped being covered by the no-argument run when the default moved.
        $defaultOutput = Invoke-IsolatedPlayer $isolatedExe $workingDirectory $defaultLog @() 'Arena.scene'
        $demoOutput = Invoke-IsolatedPlayer $isolatedExe $workingDirectory $demoLog `
            @('--scene', 'Assets\Scenes\Demo.scene') 'Demo.scene'
        $explicitOutput = Invoke-IsolatedPlayer $isolatedExe $workingDirectory $explicitLog `
            @('--scene', 'Assets\Scenes\ShadowA.scene') 'ShadowA.scene'
        Invoke-InvalidScenePlayer $isolatedExe $workingDirectory $applicationLogDir
    }
    finally
    {
        [Environment]::SetEnvironmentVariable(
            'DX12ENGINE_LOG_DIR', $oldApplicationLogDir, 'Process')
    }

    $applicationLogs = @(Get-ChildItem -LiteralPath $applicationLogDir -File)
    if ($applicationLogs.Count -ne 4)
    {
        throw "Expected one application log per Player run; found $($applicationLogs.Count)"
    }
    if (Test-Path -LiteralPath (Join-Path $isolatedPackage 'Logs'))
    {
        throw 'Package verification polluted the isolated package with application logs'
    }

    $expectedRoot = [IO.Path]::GetFullPath($isolatedPackage).TrimEnd('\')
    foreach ($runtimeLog in @($defaultOutput, $demoOutput, $explicitOutput))
    {
        if ($runtimeLog.IndexOf("runtime root: $expectedRoot", [StringComparison]::OrdinalIgnoreCase) -lt 0)
        {
            throw "Runtime log did not select the isolated package root:`n$runtimeLog"
        }
        if ($runtimeLog.IndexOf($repoRoot, [StringComparison]::OrdinalIgnoreCase) -ge 0)
        {
            throw "Runtime log leaked a repository path:`n$runtimeLog"
        }
    }

    $packageBytes = (Get-ChildItem -LiteralPath $packagePath -File -Recurse -Force |
        Measure-Object -Property Length -Sum).Sum
    Write-Output ("Phase 12.7 Player package verified outside the repository: " +
                  "{0} files, {1:N2} MiB, valid Scenes exit 0 and invalid Scene logged" -f
                  $actualFiles.Count, ($packageBytes / 1MB))
}
finally
{
    Remove-TemporaryRoot $tempRoot
}
