param(
    [Parameter(Mandatory = $true)]
    [string]$EngineDir,

    [Parameter(Mandatory = $true)]
    [string]$AssetDir,

    [Parameter(Mandatory = $true)]
    [string]$ScenePath,

    [Parameter(Mandatory = $true)]
    [string]$PackageDir
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

function Get-FullPath([string]$Path)
{
    return [IO.Path]::GetFullPath($Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Get-RelativeAssetPath([string]$Root, [string]$Path)
{
    $rootPrefix = (Get-FullPath $Root) + [IO.Path]::DirectorySeparatorChar
    $fullPath = Get-FullPath $Path
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Asset escaped the editor Assets folder: $fullPath"
    }
    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function ConvertFrom-SceneQuoted([string]$Value)
{
    return [regex]::Replace($Value, '\\([\\"])', '$1')
}

$enginePath = Get-FullPath $EngineDir
$assetPath = Get-FullPath $AssetDir
$sceneFile = Get-FullPath $ScenePath
$packagePath = Get-FullPath $PackageDir
$playerProject = Join-Path $enginePath 'Player.vcxproj'
$baselineContents = Join-Path $enginePath 'Tests\PlayerPackage.contents'

foreach ($required in @($playerProject, $baselineContents, $sceneFile))
{
    if (-not (Test-Path -LiteralPath $required -PathType Leaf))
    {
        throw "Player packaging input is missing: $required"
    }
}

$sceneRelative = Get-RelativeAssetPath $assetPath $sceneFile
if (-not $sceneRelative.StartsWith('Scenes/', [StringComparison]::OrdinalIgnoreCase) -or
    -not $sceneRelative.EndsWith('.scene', [StringComparison]::OrdinalIgnoreCase))
{
    throw 'The startup Scene must be a .scene file under Assets/Scenes'
}
$defaultScene = 'Assets/' + $sceneRelative

$entries = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([StringComparer]::OrdinalIgnoreCase)
foreach ($line in Get-Content -LiteralPath $baselineContents)
{
    $entry = $line.Trim()
    if ($entry.Length -gt 0 -and -not $entry.StartsWith('#'))
    {
        [void]$entries.Add($entry.Replace('\', '/'))
    }
}
[void]$entries.Add($sceneRelative)

function Add-RequiredAsset([string]$Relative)
{
    if ([string]::IsNullOrWhiteSpace($Relative))
    {
        return
    }
    $normalized = $Relative.Replace('\', '/').TrimStart('/')
    if ([IO.Path]::IsPathRooted($Relative) -or
        $normalized.Split('/') -contains '..')
    {
        throw "Scene asset path is not relative to Assets: $Relative"
    }
    $full = Join-Path $assetPath ($normalized.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $full -PathType Leaf))
    {
        throw "Scene asset is missing: $normalized"
    }
    [void]$entries.Add($normalized)
}

$meshAssets = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([StringComparer]::OrdinalIgnoreCase)
foreach ($line in Get-Content -LiteralPath $sceneFile)
{
    foreach ($match in [regex]::Matches(
        $line, '(mesh|texture|normal|skybox)\s+"((?:\\.|[^"])*)"'))
    {
        $kind = $match.Groups[1].Value
        $value = ConvertFrom-SceneQuoted $match.Groups[2].Value
        if ([string]::IsNullOrEmpty($value))
        {
            continue
        }
        if ($kind -eq 'skybox')
        {
            foreach ($face in @('px', 'nx', 'py', 'ny', 'pz', 'nz'))
            {
                Add-RequiredAsset "Skyboxes/$value/$face.png"
            }
        }
        elseif ($kind -eq 'mesh')
        {
            if (-not $value.StartsWith('#'))
            {
                Add-RequiredAsset $value
                [void]$meshAssets.Add($value.Replace('\', '/'))
            }
        }
        else
        {
            Add-RequiredAsset $value
        }
    }
}

# OBJ materials are the only transitive asset relationship in the runtime.
# Match the loader: mtllib lives beside the OBJ, and map_Kd resolves by bare
# file name beside it, first as written and then with a missing .png restored.
foreach ($mesh in $meshAssets)
{
    $meshFull = Join-Path $assetPath ($mesh.Replace('/', '\'))
    $meshDir = Split-Path -Parent $meshFull
    foreach ($line in Get-Content -LiteralPath $meshFull)
    {
        if ($line -notmatch '^\s*mtllib\s+(.+?)\s*$')
        {
            continue
        }
        $mtlFull = Join-Path $meshDir $Matches[1]
        if (-not (Test-Path -LiteralPath $mtlFull -PathType Leaf))
        {
            continue
        }
        Add-RequiredAsset (Get-RelativeAssetPath $assetPath $mtlFull)
        foreach ($mtlLine in Get-Content -LiteralPath $mtlFull)
        {
            if ($mtlLine -notmatch '^\s*map_Kd\s+(.+?)\s*$')
            {
                continue
            }
            $bareName = Split-Path -Leaf $Matches[1]
            foreach ($candidate in @($bareName, $bareName + '.png'))
            {
                $textureFull = Join-Path $meshDir $candidate
                if (Test-Path -LiteralPath $textureFull -PathType Leaf)
                {
                    Add-RequiredAsset (Get-RelativeAssetPath $assetPath $textureFull)
                    break
                }
            }
        }
    }
}

$inputDir = Join-Path (Split-Path -Parent $enginePath) 'Output\PackageInputs'
New-Item -ItemType Directory -Path $inputDir -Force | Out-Null
$contentsPath = Join-Path $inputDir (([guid]::NewGuid().ToString('N')) + '.contents')
@(
    '# Generated by the Editor for one Player package.'
    '# Baseline runtime content plus the selected Scene dependency closure.'
) + @($entries | Sort-Object) |
    Set-Content -LiteralPath $contentsPath -Encoding UTF8

$msbuild = Get-Command MSBuild.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source
if (-not $msbuild)
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf)
    {
        $msbuild = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf))
{
    throw 'MSBuild.exe was not found. Install Visual Studio with Desktop development with C++.'
}

Write-Output "Packaging startup Scene: $defaultScene"
Write-Output "Package output: $packagePath"
Write-Output "MSBuild: $msbuild"

try
{
    & $msbuild $playerProject /m /t:Build /p:Configuration=Release /p:Platform=x64 `
        "/p:DefaultPlayerScene=$defaultScene" `
        "/p:PlayerPackageDir=$packagePath" `
        "/p:PlayerAssetSourceDir=$assetPath" `
        "/p:PlayerContentFile=$contentsPath"
    if ($LASTEXITCODE -ne 0)
    {
        throw "MSBuild failed with exit code $LASTEXITCODE"
    }
}
finally
{
    Remove-Item -LiteralPath $contentsPath -Force -ErrorAction SilentlyContinue
}

Write-Output "Player package completed: $packagePath"
