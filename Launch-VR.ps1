function Get-VrLaunchOptions {
    param(
        [string]$Root = $PSScriptRoot,
        [ValidateRange(0, 2)][int]$Mode = 0
    )
    $Root = [System.IO.Path]::GetFullPath($Root)
    $executable = Join-Path $Root 'out\build\relwithdebinfo\skate3.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Falta la compilacion VR: $executable`nConsulta docs/PCVR_BUILD.md para prepararla."
    }
    $game = Join-Path $Root 'game'
    if (-not (Test-Path -LiteralPath (Join-Path $game 'default.xex') -PathType Leaf)) {
        throw "Falta el juego extraido en: $game`nConsulta docs/PCVR_BUILD.md."
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
    $instructions.Text = "Conecta el visor, entra en Quest Link y conecta tu mando.`nElige como quieres jugar:"
    $instructions.AutoSize = $true
    $instructions.MaximumSize = New-Object System.Drawing.Size(430, 0)
    $instructions.Margin = New-Object System.Windows.Forms.Padding(3, 3, 3, 12)
    $layout.Controls.Add($instructions)

    $modes = @('VR inmersiva', 'Ventana 3D', 'Ventana 3D + passthrough')
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
                    'No se puede iniciar Skate 3 VR', 'OK', 'Error') | Out-Null
            }
        })
        $layout.Controls.Add($button)
    }

    $notes = New-Object System.Windows.Forms.Label
    $notes.Text = "Ajustes de calidad y camara: RB + Start > Video.`nPassthrough requiere activarlo antes en la app Meta Link."
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
