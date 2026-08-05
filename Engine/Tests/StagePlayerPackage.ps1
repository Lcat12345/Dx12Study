param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [string]$PlayerOutputDir,

    [Parameter(Mandatory = $true)]
    [string]$AssetSourceDir,

    [Parameter(Mandatory = $true)]
    [string]$PackageDir,

    [Parameter(Mandatory = $true)]
    [string]$DefaultScene,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path)
{
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Get-PackageRelativePath([string]$Root, [string]$Path)
{
    $rootPrefix = (Get-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    $fullPath = Get-FullPath $Path
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Package file escaped the package root: $fullPath"
    }
    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Assert-NoForbiddenContent([string]$Root)
{
    $forbiddenExtensions = @('.slnx', '.vcxproj', '.cpp', '.c', '.h', '.hpp', '.hlsl')
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse -Force)
    {
        if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant())
        {
            throw "Forbidden source/project file in Player package: $($file.FullName)"
        }
        if ($file.Name -ieq 'imgui.ini' -or $file.Name -imatch '^imgui.*\.(ini|cpp|c|h|hpp)$')
        {
            throw "Forbidden ImGui file in Player package: $($file.FullName)"
        }
    }
}

$repoRootPath = Get-FullPath $RepoRoot
$outputRoot = Get-FullPath (Join-Path $repoRootPath 'Output')
$playerOutputPath = Get-FullPath $PlayerOutputDir
$assetSource = Get-FullPath $AssetSourceDir
$packagePath = Get-FullPath $PackageDir
$outputPrefix = $outputRoot + [IO.Path]::DirectorySeparatorChar

if (-not $packagePath.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase))
{
    throw "Refusing to recreate a package outside the repository Output directory: $packagePath"
}
if ($packagePath -eq $outputRoot -or $packagePath -eq $playerOutputPath)
{
    throw "Refusing to recreate an unsafe package path: $packagePath"
}

$playerExe = Join-Path $playerOutputPath 'Player.exe'
$shaderSource = Join-Path $playerOutputPath 'Shaders'
$linkMap = Join-Path $playerOutputPath 'Player.map'

foreach ($required in @($playerExe, $assetSource, $shaderSource, $linkMap))
{
    if (-not (Test-Path -LiteralPath $required))
    {
        throw "Player package input is missing: $required"
    }
}

$mapText = Get-Content -LiteralPath $linkMap -Raw
foreach ($symbol in @('ImGui::', 'ImGui_Impl', 'DebugUI', 'AssetBrowser',
                      'EditorSession', 'EditorApp'))
{
    if ($mapText.IndexOf($symbol, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    {
        throw "Player.map contains forbidden Editor/ImGui symbol '$symbol'"
    }
}

$playerBinary = [Text.Encoding]::ASCII.GetString(
    [IO.File]::ReadAllBytes($playerExe))
if ($playerBinary.IndexOf($repoRootPath, [StringComparison]::OrdinalIgnoreCase) -ge 0)
{
    throw 'Player.exe contains an absolute repository path'
}

if (Test-Path -LiteralPath $packagePath)
{
    Remove-Item -LiteralPath $packagePath -Recurse -Force
}
New-Item -ItemType Directory -Path $packagePath | Out-Null

Copy-Item -LiteralPath $playerExe -Destination $packagePath

# Assets are staged from a DECLARED list, not by copying Assets\ wholesale.
# See PlayerPackage.contents for why - in short, a blanket copy made the
# package depend on untracked files sitting in a developer's folder and
# shipped 43 MB of non-redistributable art no Scene references.
$contentsFile = Join-Path $PSScriptRoot 'PlayerPackage.contents'
if (-not (Test-Path -LiteralPath $contentsFile -PathType Leaf))
{
    throw "Player package content list is missing: $contentsFile"
}

$stagedAssets = New-Object System.Collections.Generic.List[string]
foreach ($line in Get-Content -LiteralPath $contentsFile)
{
    $entry = $line.Trim()
    if ($entry.Length -eq 0 -or $entry.StartsWith('#'))
    {
        continue
    }

    $entryPath = Join-Path $assetSource ($entry.Replace('/', '\'))
    # -Path, not -LiteralPath: entries are allowed to be wildcards.
    # NOT named $matches - that is a PowerShell automatic variable.
    $matchedFiles = @(Get-ChildItem -Path $entryPath -File -ErrorAction SilentlyContinue)
    if ($matchedFiles.Count -eq 0)
    {
        # A shipped Scene that names a missing asset must fail HERE, where the
        # line that asked for it is known, rather than as a texture that
        # quietly falls back to white on a user's machine.
        throw "Player package content entry matched no file: $entry"
    }

    foreach ($file in $matchedFiles)
    {
        $relative = (Get-FullPath $file.FullName).Substring((Get-FullPath $assetSource).Length + 1)
        $destination = Join-Path (Join-Path $packagePath 'Assets') $relative
        $destinationDir = Split-Path -Parent $destination
        if (-not (Test-Path -LiteralPath $destinationDir))
        {
            New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destination
        $stagedAssets.Add($relative.Replace('\', '/'))
    }
}

Copy-Item -LiteralPath $shaderSource -Destination $packagePath -Recurse

# A DLL beside Player.exe is an explicitly deployed, non-system runtime dependency.
# The static CRT build currently produces none; Windows system DLLs are never copied.
$runtimeDlls = @(Get-ChildItem -LiteralPath $playerOutputPath -File -Filter '*.dll')
foreach ($dll in $runtimeDlls)
{
    Copy-Item -LiteralPath $dll.FullName -Destination $packagePath
}

$defaultScenePath = Join-Path $packagePath ($DefaultScene.Replace('/', '\'))
if (-not (Test-Path -LiteralPath $defaultScenePath -PathType Leaf))
{
    throw "Default Player Scene was not staged: $DefaultScene"
}
if (@(Get-ChildItem -LiteralPath (Join-Path $packagePath 'Shaders') -File -Filter '*.cso').Count -eq 0)
{
    throw 'No compiled shaders were staged'
}

Assert-NoForbiddenContent $packagePath

$gitCommit = (& git -C $repoRootPath rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitCommit))
{
    throw 'Could not record the source commit in package-manifest.txt'
}
$sourceState = if ((& git -C $repoRootPath status --porcelain --untracked-files=normal).Count -eq 0)
{
    'clean'
}
else
{
    'dirty'
}

# Everything staged must be in the commit the manifest is about to name.
#
# Without this, "commit=<sha> source_state=clean" reads as "check out this
# commit and you get this package" while the package could still be carrying
# files that exist only on this machine - which is exactly the state the
# blanket Assets\ copy produced. An untracked asset is either something that
# should be committed or something that should not ship; both are decisions
# for a person, so fail and say which file forced the question.
$trackedAssets = @(& git -C $repoRootPath ls-files --error-unmatch -- 'Engine/Assets' 2>$null |
    ForEach-Object { $_ -replace '^Engine/Assets/', '' })
if ($LASTEXITCODE -ne 0)
{
    throw 'Could not list tracked assets; the package cannot claim a commit'
}
$untrackedStaged = @($stagedAssets | Where-Object { $trackedAssets -notcontains $_ })
if ($untrackedStaged.Count -gt 0)
{
    throw ("Player package would ship files git does not track, so the commit " +
           "in the manifest would not reproduce it: " + ($untrackedStaged -join ', '))
}

$manifestName = 'package-manifest.txt'
$fileList = @(Get-ChildItem -LiteralPath $packagePath -File -Recurse -Force |
    ForEach-Object { Get-PackageRelativePath $packagePath $_.FullName })
$fileList += $manifestName
$fileList = @($fileList | Sort-Object -Unique)
$dllPolicy = if ($runtimeDlls.Count -eq 0)
{
    'none (static CRT; Windows system DLLs excluded)'
}
else
{
    ($runtimeDlls.Name | Sort-Object) -join ', '
}

$manifest = @(
    'Dx12Engine Player package manifest'
    "configuration=$Configuration"
    'platform=x64'
    "commit=$($gitCommit.Trim())"
    "source_state=$sourceState"
    "default_scene=$($DefaultScene.Replace('\', '/'))"
    "runtime_dlls=$dllPolicy"
    # Says HOW the asset set was chosen, so "commit=" above is read as the
    # reproducibility claim it now actually is.
    "asset_policy=declared in Engine/Tests/PlayerPackage.contents; all staged assets are tracked"
    ''
    '[files]'
) + $fileList

$manifestPath = Join-Path $packagePath $manifestName
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding UTF8

$packageBytes = (Get-ChildItem -LiteralPath $packagePath -File -Recurse -Force |
    Measure-Object -Property Length -Sum).Sum
Write-Output ("Player package staged: {0} files, {1:N2} MiB at {2}" -f
              $fileList.Count, ($packageBytes / 1MB), $packagePath)
