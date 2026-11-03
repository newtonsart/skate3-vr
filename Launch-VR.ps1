function Get-VrLaunchOptions {
    param(
        [string]$Root = $PSScriptRoot,
        [ValidateRange(0, 2)][int]$Mode = 0
    )
    $Root = [System.IO.Path]::GetFullPath($Root)
    $executable = Join-Path $Root 'out\build\relwithdebinfo\skate3.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "VR build not found: $executable`nSee docs/PCVR_BUILD.md for setup instructions."
    }
    $game = Join-Path $Root 'game'
    if (-not (Test-Path -LiteralPath (Join-Path $game 'default.xex') -PathType Leaf)) {
        throw "Extracted game files not found in: $game`nSee docs/PCVR_BUILD.md."
    }
    # Preserve the profile used by the documented VR command, including existing saves.
    $userData = Join-Path $Root 'out\runtime-test'
    return @{
        FilePath = $executable
        WorkingDirectory = $Root
        ArgumentList = @(
            '--gpu_backend=vulkan'
            '--skate3_vr=true'
            "--skate3_vr_mode=$Mode"
            ('--game_data_root="{0}"' -f $game)
            ('--user_data_root="{0}"' -f $userData)
            '--skate3_native_render_scene_msaa=1'
            '--skate3_vr_share_frame_work=true'
        )
    }
}

function New-VrLauncherForm {
    param([string]$Root = $PSScriptRoot)
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'Skate 3 VR'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
    $form.AutoSize = $true
    $form.AutoSizeMode = 'GrowAndShrink'
    $form.Tag = $Root

    $layout = New-Object System.Windows.Forms.FlowLayoutPanel
    $layout.FlowDirection = 'TopDown'
    $layout.WrapContents = $false
    $layout.AutoSize = $true
    $layout.AutoSizeMode = 'GrowAndShrink'
    $layout.Padding = New-Object System.Windows.Forms.Padding(16)
    $form.Controls.Add($layout)

    $instructions = New-Object System.Windows.Forms.Label
    $instructions.Text = "Connect your headset and start PCVR (Quest Link on Meta).`nConnect your gamepad, then choose a mode:"
    $instructions.AutoSize = $true
    $instructions.MaximumSize = New-Object System.Drawing.Size(430, 0)
    $instructions.Margin = New-Object System.Windows.Forms.Padding(3, 3, 3, 12)
    $layout.Controls.Add($instructions)

    $modes = @('Immersive VR', '3D window', '3D window + passthrough')
    for ($mode = 0; $mode -lt $modes.Count; $mode++) {
        $button = New-Object System.Windows.Forms.Button
        $button.Text = $modes[$mode]
        $button.Size = New-Object System.Drawing.Size(430, 44)
        $button.Tag = $mode
        $button.Add_Click({
            param($sender, $eventArgs)
            $window = $sender.FindForm()
            try {
                $options = Get-VrLaunchOptions -Root $window.Tag -Mode ([int]$sender.Tag)
                Start-Process @options -ErrorAction Stop | Out-Null
                $window.Close()
            } catch {
                [System.Windows.Forms.MessageBox]::Show($window, $_.Exception.Message,
                    'Unable to start Skate 3 VR', 'OK', 'Error') | Out-Null
            }
        })
        $layout.Controls.Add($button)
    }

    $notes = New-Object System.Windows.Forms.Label
    $notes.Text = "Quality and camera settings: RB + Start > Video.`nPassthrough must be enabled in Meta Link first."
    $notes.AutoSize = $true
    $notes.MaximumSize = New-Object System.Drawing.Size(430, 0)
    $notes.Margin = New-Object System.Windows.Forms.Padding(3, 12, 3, 3)
    $layout.Controls.Add($notes)
    return $form
}

if ($MyInvocation.InvocationName -ne '.') {
    $launcher = New-VrLauncherForm
    try { [void]$launcher.ShowDialog() }
    finally { $launcher.Dispose() }
}
