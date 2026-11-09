$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility
. (Join-Path $PSScriptRoot '..\Launch-VR.ps1')

function Assert($Condition, $Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-LaunchError($Root, $Text) {
    try { Get-VrLaunchOptions -Root $Root | Out-Null }
    catch {
        Assert ($_.Exception.Message.Contains($Text)) "Unexpected error: $_"
        return
    }
    throw "Expected an error containing: $Text"
}
function Assert-Error([scriptblock]$Action, [string]$Text) {
    try { & $Action | Out-Null }
    catch { Assert ($_.Exception.Message.Contains($Text)) "Unexpected error: $_"; return }
    throw "Expected error: $Text"
}

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("skate3 launcher & [test] '" + [char]0xE1 + ' ' + [guid]::NewGuid())
$fixture = [System.IO.Path]::GetFullPath($fixture)
function global:Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string]$WorkingDirectory, [string[]]$ArgumentList)
    $script:launched = @{ FilePath = $FilePath; WorkingDirectory = $WorkingDirectory; ArgumentList = $ArgumentList }
}
$script:badHash = $false
$realFileHash = (Get-Command Get-VrFileHash).ScriptBlock
function Get-VrFileHash {
    param([string]$Path)
    if ($script:badHash) { return 'invalid' }
    $hash = 'eb9ef9109dfa6d940df2e156e7eaeda4603d2b2319ca6451f324b1c27f2b1f4c'
    if ($Path.EndsWith('EAWebkit.xexp')) { $hash = '5d4a308d2a6c768fc27c8b62ccb6661171dc504f8c0011a3e116cf0074e09438' }
    return $hash
}
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Assert-LaunchError $fixture 'VR build not found'
    $build = New-Item -ItemType Directory -Path (Join-Path $fixture 'out\build\relwithdebinfo')
    New-Item -ItemType File -Path (Join-Path $build.FullName 'skate3.exe') | Out-Null
    Assert ((& $realFileHash (Join-Path $build.FullName 'skate3.exe')) -eq 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855') 'SHA256 self-check failed'
    Assert-LaunchError $fixture 'Title Update 3'
    $setupOptions = Get-VrLaunchOptions -Root $fixture -Setup
    Assert ($setupOptions.ArgumentList -contains '--skate3_setup_only=true') 'Setup did not select install-only mode'
    Assert ($setupOptions.ArgumentList -contains '--skate3_vr=false') 'Setup requires a headset'
    New-Item -ItemType Directory -Path (Join-Path $fixture 'game\data\webkit') | Out-Null
    foreach ($name in @('default.xex','default.xexp','data\webkit\EAWebkit.xex','data\webkit\EAWebkit.xexp')) {
        New-Item -ItemType File -Path (Join-Path $fixture ('game\' + $name)) | Out-Null
    }
    $script:badHash = $true
    Assert-LaunchError $fixture 'Title Update 3'
    $script:badHash = $false

    $incompleteMarker=Join-Path $fixture 'game\.skate3-vr-installing'
    [IO.File]::WriteAllText($incompleteMarker, '')
    Assert-LaunchError $fixture 'Title Update 3'
    [IO.File]::Delete($incompleteMarker)
    $incompleteMarker=Join-Path $fixture 'game\.skate3_install_incomplete'
    [IO.File]::WriteAllText($incompleteMarker, '')
    Assert-LaunchError $fixture 'Title Update 3'
    [IO.File]::Delete($incompleteMarker)
    foreach ($mode in 0..2) {
        $options = Get-VrLaunchOptions -Root $fixture -Mode $mode
        Assert ($options.FilePath -eq (Join-Path $build.FullName 'skate3.exe')) 'Wrong executable'
        Assert ($options.WorkingDirectory -eq $fixture) 'Wrong working directory'
        Assert ($options.ArgumentList -contains "--skate3_vr_mode=$mode") 'Wrong VR mode'
        Assert ($options.ArgumentList -contains '--skate3_vr=true') 'VR not enabled'
        Assert ($options.ArgumentList -contains '--gpu_backend=vulkan') 'Vulkan not selected'
        Assert ($options.ArgumentList -contains '--skate3_vr_perf_trace=true') 'VR timing trace not enabled'
        Assert ($options.ArgumentList -contains '--skate3_vr_gpu_timing=true') 'GPU timing not enabled'
        Assert ($options.ArgumentList -contains '--skate3_native_render_scene_perf_log=true') 'Native timing not enabled'
        Assert ($options.ArgumentList -contains ('--game_data_root="{0}\game"' -f $fixture)) 'Game path lost its quotes'
        Assert ($options.ArgumentList -contains ('--user_data_root="{0}\out\runtime-test"' -f $fixture)) 'User profile changed'
    }
    $savedPrefetch = $env:SKATE3_VR_PREFETCH_FRAME
    try {
        $env:SKATE3_VR_PREFETCH_FRAME = '1'
        foreach ($mode in 0..2) {
            $options = Get-VrLaunchOptions -Root $fixture -Mode $mode
            Assert (($options.ArgumentList -contains '--skate3_vr_prefetch_frame=true') -eq ($mode -eq 0)) 'XR prefetch must be immersive only'
        }
        $options = Get-VrLaunchOptions -Root $fixture -Setup
        Assert ($options.ArgumentList -notcontains '--skate3_vr_prefetch_frame=true') 'Setup must skip XR prefetch'
    } finally { $env:SKATE3_VR_PREFETCH_FRAME = $savedPrefetch }
    Assert (-not (Test-Path -LiteralPath (Join-Path $fixture 'out\runtime-test'))) 'Launcher modified user data'
    foreach ($mode in 0..2) {
        $form = New-VrLauncherForm -Root $fixture
        try {
            $buttons = @($form.Controls[0].Controls | Where-Object { $_.Name -like 'Mode*' })
            Assert ($buttons.Count -eq 3) 'Expected three mode buttons'
            Assert (($buttons.Tag -join ',') -eq '0,1,2') 'Button modes do not match OpenXR modes'
            $form.Show()
            foreach ($button in $buttons) {
                Assert ($button.Parent.ClientRectangle.Contains($button.Bounds)) 'A mode button is clipped'
            }
            $script:launched = $null
            $buttons[$mode].PerformClick()
            Assert ($null -ne $script:launched) 'Click did not invoke the launcher'
            Assert ($script:launched.ArgumentList -contains "--skate3_vr_mode=$mode") 'Click launched the wrong mode'
        } finally { $form.Dispose() }
    }
    $gameFolder = Join-Path $fixture 'Custom game'
    Copy-Item -LiteralPath (Join-Path $fixture 'game') -Destination $gameFolder -Recurse
    $saveFolder = Join-Path $fixture 'Custom saves'
    [void][IO.Directory]::CreateDirectory($saveFolder)
    [IO.File]::WriteAllText((Join-Path $saveFolder 'settings.toml'), 'camera = 1')
    [IO.File]::WriteAllText((Join-Path $saveFolder 'main-progress'), 'keep this save')
    [IO.File]::WriteAllText((Join-Path $fixture 'install.ini'), "[Paths]`nGameDataRoot=$gameFolder`nUserDataRoot=$saveFolder`n", (New-Object Text.UTF8Encoding($false)))
    $info = Get-VrInstallation $fixture
    Assert ($info.GameRoot -eq $gameFolder -and $info.UserRoot -eq $saveFolder) 'Custom installation paths were ignored'
    $profileId = New-VrSaveProfile -UserRoot $saveFolder -Name 'Second career'
    $profiles = @(Get-VrSaveProfiles $saveFolder)
    Assert ($profiles.Count -eq 2) 'New save profile is missing'
    $newData = Join-Path $saveFolder ('save-profiles\' + $profileId)
    Assert (Test-Path -LiteralPath (Join-Path $newData 'settings.toml')) 'Graphics settings were not preserved'
    Assert (-not (Test-Path -LiteralPath (Join-Path $newData 'main-progress'))) 'New profile copied the old progress'
    Assert ([IO.File]::ReadAllText((Join-Path $saveFolder 'main-progress')) -eq 'keep this save') 'Original progress was changed'
    $profileOptions = Get-VrLaunchOptions -Root $fixture -ProfileId $profileId
    Assert ($profileOptions.ArgumentList -contains ('--user_data_root="{0}"' -f $newData)) 'Selected profile uses the wrong save folder'
    Assert-Error { New-VrSaveProfile -UserRoot $saveFolder -Name 'Second career' } 'already exists'
    Assert-Error { Get-VrLaunchOptions -Root $fixture -ProfileId ([guid]::NewGuid().ToString()) } 'no longer exists'
    $statePath = Join-Path $saveFolder 'launcher-profiles.json'
    $goodState = [IO.File]::ReadAllText($statePath)
    [IO.File]::WriteAllText($statePath, '{}')
    Assert-Error { Get-VrSaveProfiles $saveFolder } 'Cannot read save profiles'
    Assert ([IO.File]::ReadAllText($statePath) -eq '{}') 'Bad metadata was overwritten'
    [IO.File]::WriteAllText($statePath, '{"SelectedProfile":"default","Profiles":[{"Id":"..\\escape","Name":"Bad"}]}')
    Assert-Error { Get-VrSaveProfiles $saveFolder } 'Cannot read save profiles'
    [IO.File]::WriteAllText($statePath, $goodState)
    $installedBin = New-Item -ItemType Directory -Path (Join-Path $fixture 'bin')
    New-Item -ItemType File -Path (Join-Path $installedBin.FullName 'skate3.exe') | Out-Null
    Assert ((Get-VrInstallation $fixture).Executable -eq (Join-Path $installedBin.FullName 'skate3.exe')) 'Installed layout was not detected'
    $form = New-VrLauncherForm -Root $fixture
    try {
        $selector = $form.Controls[0].Controls['SaveProfile']
        Assert ($selector.Items.Count -eq 2) 'Profile selector omitted a save'
        Assert ($selector.SelectedItem -eq 'Second career') 'Profile selector displays internal metadata instead of its name'
    }
    finally { $form.Dispose() }
    Write-Output 'PASS: installed/development paths, TU3 verification, setup mode, independent saves, metadata validation and launcher actions'
} finally {
    $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($fixture.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
