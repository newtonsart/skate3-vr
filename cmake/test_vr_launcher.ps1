$ErrorActionPreference = 'Stop'
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

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("skate3 launcher & [test] '" + [char]0xE1 + ' ' + [guid]::NewGuid())
$fixture = [System.IO.Path]::GetFullPath($fixture)
function global:Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string]$WorkingDirectory, [string[]]$ArgumentList)
    $script:launched = @{ FilePath = $FilePath; WorkingDirectory = $WorkingDirectory; ArgumentList = $ArgumentList }
}
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Assert-LaunchError $fixture 'compilacion VR'
    $build = New-Item -ItemType Directory -Path (Join-Path $fixture 'out\build\relwithdebinfo')
    New-Item -ItemType File -Path (Join-Path $build.FullName 'skate3.exe') | Out-Null
    Assert-LaunchError $fixture 'juego extraido'
    New-Item -ItemType Directory -Path (Join-Path $fixture 'game') | Out-Null
    New-Item -ItemType File -Path (Join-Path $fixture 'game\default.xex') | Out-Null

    foreach ($mode in 0..2) {
        $options = Get-VrLaunchOptions -Root $fixture -Mode $mode
        Assert ($options.FilePath -eq (Join-Path $build.FullName 'skate3.exe')) 'Wrong executable'
        Assert ($options.WorkingDirectory -eq $fixture) 'Wrong working directory'
        Assert ($options.ArgumentList -contains "--skate3_vr_mode=$mode") 'Wrong VR mode'
        Assert ($options.ArgumentList -contains '--skate3_vr=true') 'VR not enabled'
        Assert ($options.ArgumentList -contains '--gpu_backend=vulkan') 'Vulkan not selected'
        Assert ($options.ArgumentList -contains ('--game_data_root="{0}\game"' -f $fixture)) 'Game path lost its quotes'
        Assert ($options.ArgumentList -contains ('--user_data_root="{0}\out\runtime-test"' -f $fixture)) 'User profile changed'
    }
    Assert (-not (Test-Path -LiteralPath (Join-Path $fixture 'out\runtime-test'))) 'Launcher modified user data'
    foreach ($mode in 0..2) {
        $form = New-VrLauncherForm -Root $fixture
        try {
            $buttons = @($form.Controls[0].Controls | Where-Object { $_ -is [System.Windows.Forms.Button] })
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
    Write-Output 'PASS: VR launcher paths, mode arguments, missing files, unchanged profile, form layout and button actions'
} finally {
    $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($fixture.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
