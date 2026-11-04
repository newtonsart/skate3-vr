param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\out\build\relwithdebinfo'),
    [string]$Compiler = (Join-Path $PSScriptRoot '..\out\tools\InnoSetup\ISCC.exe'),
    [string]$Version = '0.2.5-alpha'
)
$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
if ($Version -notmatch '^[0-9A-Za-z.-]+$') { throw 'Invalid package version' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = [IO.Path]::GetFullPath($BuildDirectory)
$out = Join-Path $root 'out\packages'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Install the Inno Setup compiler and pass its path with -Compiler.' }
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
if ($cache -notcontains 'SKATE3_ENABLE_OPENXR:BOOL=ON') { throw 'Packaging requires an OpenXR-enabled build.' }
$executable = Join-Path $build 'skate3.exe'
# A played executable may contain the user's game icon. Relink before packaging.
if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable -ErrorAction Stop }
& cmake --build $build --target skate3 --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Clean game relink failed' }
$payload = Join-Path $out ('payload-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory((Join-Path $payload 'bin'))
Copy-Item -LiteralPath $executable -Destination (Join-Path $payload 'bin\skate3.exe')
$runtime = @(Get-ChildItem -LiteralPath $build -Filter 'rexruntime*.dll' -File)
if ($runtime.Count -ne 1) { throw 'Expected exactly one runtime DLL in the build directory' }
Copy-Item -LiteralPath $runtime[0].FullName -Destination (Join-Path $payload ('bin\' + $runtime[0].Name))
foreach ($name in @('Skate3-VR.cmd','Launch-VR.ps1','README.md')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $payload $name)
}
[void][IO.Directory]::CreateDirectory((Join-Path $payload 'docs'))
Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Filter '*.md' -File | Copy-Item -Destination (Join-Path $payload 'docs')
foreach ($name in @('banner.png','banner-light.png')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $payload }
$licenseRoots = @{}
foreach ($entry in @(@('REXSDK_DIR','ReXGlue'), @('SKATE3_OPENXR_SDK_DIR','OpenXR'))) {
    $line = @($cache | Where-Object { $_ -match ('^' + $entry[0] + ':(PATH|STRING)=') })
    if ($line.Count -ne 1) { throw "Missing dependency path: $($entry[0])" }
    $licenseRoots[$entry[1]] = ($line[0] -split '=',2)[1]
}
foreach ($dependency in $licenseRoots.Keys) {
    $dependencyRoot = [IO.Path]::GetFullPath($licenseRoots[$dependency])
    Get-ChildItem -LiteralPath $dependencyRoot -File -Recurse | Where-Object {
        $_.FullName -notlike '*\out\build\*' -and $_.Name -match '^(LICEN[SC]E|COPYING|NOTICE|COPYRIGHT)([.-].*)?$'
    } | ForEach-Object {
        $target = Join-Path $payload ('licenses\' + $dependency + '\' + $_.FullName.Substring($dependencyRoot.Length + 1))
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
        Copy-Item -LiteralPath $_.FullName -Destination $target
    }
}
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($payload.Length + 1)
    if ($relative -notmatch '^(bin\\(skate3\.exe|rexruntime[^\\]*\.dll)|docs\\[^\\]+\.md|licenses\\.+|Skate3-VR\.cmd|Launch-VR\.ps1|README\.md|banner(-light)?\.png)$') {
        throw "Unexpected package file: $relative"
    }
}
$zip = Join-Path $out "Skate3-VR-Windows-$Version.zip"
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $zip -Force
& $Compiler ("/DPayloadDir=$payload") ("/DPackageVersion=$Version") ("/O$out") (Join-Path $root 'installer\Skate3-VR.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
. (Join-Path $root 'Launch-VR.ps1')
@($zip,(Join-Path $out "Skate3-VR-Setup-$Version.exe")) |
    ForEach-Object { "$(Get-VrFileHash $_)  $([IO.Path]::GetFileName($_))" } |
    Set-Content -LiteralPath (Join-Path $out "SHA256SUMS-$Version.txt") -Encoding ASCII
Write-Output "Packages ready in $out"
