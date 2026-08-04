param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $engineRoot

function Read-Project([string]$Name)
{
    $path = Join-Path $engineRoot "$Name.vcxproj"
    if (-not (Test-Path -LiteralPath $path))
    {
        throw "Missing project: $path"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-DoesNotContain([string]$Text, [string[]]$Forbidden, [string]$Label)
{
    foreach ($value in $Forbidden)
    {
        if ($Text.Contains($value))
        {
            throw "$Label contains forbidden dependency '$value'"
        }
    }
}

$engineProject = Read-Project 'Engine'
$gameProject = Read-Project 'Game'
$editorProject = Read-Project 'Editor'
$playerProject = Read-Project 'Player'

if (-not $engineProject.Contains('<ConfigurationType>StaticLibrary</ConfigurationType>'))
{
    throw 'Engine is not a static library'
}
if (-not $gameProject.Contains('<ConfigurationType>StaticLibrary</ConfigurationType>'))
{
    throw 'Game is not a static library'
}
if (-not $editorProject.Contains('<ConfigurationType>Application</ConfigurationType>'))
{
    throw 'Editor is not an application'
}
if (-not $playerProject.Contains('<ConfigurationType>Application</ConfigurationType>'))
{
    throw 'Player is not an application'
}

Assert-DoesNotContain $engineProject @('Source\Game\', 'ImGuiLayer', 'imgui') 'Engine.vcxproj'
Assert-DoesNotContain $gameProject @('DebugUI', 'AssetBrowser', 'EditorSession', 'EditorApp', 'imgui') 'Game.vcxproj'
Assert-DoesNotContain $playerProject @('DebugUI', 'AssetBrowser', 'EditorSession', 'EditorApp', 'ImGui', 'imgui') 'Player.vcxproj'

[xml]$playerXml = $playerProject
$namespace = New-Object System.Xml.XmlNamespaceManager($playerXml.NameTable)
$namespace.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
$references = @($playerXml.SelectNodes('//msb:ProjectReference', $namespace) |
    ForEach-Object { Split-Path -Leaf $_.Include } |
    Sort-Object)
if (($references -join ',') -ne 'Engine.vcxproj,Game.vcxproj')
{
    throw "Player references must be exactly Engine and Game; found: $($references -join ', ')"
}

$mapPath = Join-Path $repoRoot "Output\x64\$Configuration\Player\Player.map"
if (-not (Test-Path -LiteralPath $mapPath))
{
    throw "Missing Player link map: $mapPath"
}
$map = Get-Content -LiteralPath $mapPath -Raw
Assert-DoesNotContain $map @('ImGui::', 'ImGui_Impl', 'DebugUI', 'AssetBrowser', 'EditorSession', 'EditorApp') 'Player.map'

Write-Output "Phase 12.4 $Configuration project/link boundary verified"
