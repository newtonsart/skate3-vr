Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop

function Get-VrFileHash {
    param([string]$Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}

function Get-VrInstallation {
    param([string]$Root = $PSScriptRoot)
    $Root = [IO.Path]::GetFullPath($Root)
    $executable = Join-Path $Root 'bin\skate3.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        $executable = Join-Path $Root 'out\build\relwithdebinfo\skate3.exe'
    }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw 'VR build not found. Install the Windows package, or follow docs/PCVR_BUILD.md.'
    }
    $game = Join-Path $Root 'game'
    $userData = Join-Path $Root 'out\runtime-test'
    $config = Join-Path $Root 'install.ini'
    if (Test-Path -LiteralPath $config -PathType Leaf) {
        $paths = @{}
        foreach ($line in Get-Content -LiteralPath $config -Encoding UTF8) {
            if ($line -match '^(GameDataRoot|UserDataRoot)=(.+)$') {
                if ($paths.ContainsKey($Matches[1]) -or -not [IO.Path]::IsPathRooted($Matches[2])) {
                    throw 'Invalid installation paths in install.ini. Run Setup again.'
                }
                $paths[$Matches[1]] = [IO.Path]::GetFullPath($Matches[2])
            }
        }
        if ($paths.Count -ne 2) { throw 'Incomplete install.ini. Run Setup again.' }
        $game = $paths.GameDataRoot
        $userData = $paths.UserDataRoot
    }
    return [pscustomobject]@{ Root = $Root; Executable = $executable; GameRoot = $game; UserRoot = $userData }
}

function Test-VrGameReady {
    param([string]$GameRoot)
    if (Test-Path -LiteralPath (Join-Path $GameRoot '.skate3-vr-installing')) { return $false }
    foreach ($name in @('default.xex','data\webkit\EAWebkit.xex','default.xexp','data\webkit\EAWebkit.xexp')) {
        if (-not (Test-Path -LiteralPath (Join-Path $GameRoot $name) -PathType Leaf)) { return $false }
    }
    $defaultHash = Get-VrFileHash (Join-Path $GameRoot 'default.xexp')
    $webkitHash = Get-VrFileHash (Join-Path $GameRoot 'data\webkit\EAWebkit.xexp')
    return $defaultHash -eq 'eb9ef9109dfa6d940df2e156e7eaeda4603d2b2319ca6451f324b1c27f2b1f4c' -and
        $webkitHash -eq '5d4a308d2a6c768fc27c8b62ccb6661171dc504f8c0011a3e116cf0074e09438'
}

function Read-VrLauncherState {
    param([string]$UserRoot)
    $state = [pscustomobject]@{ SelectedProfile = 'default'; Profiles = @() }
    $path = Join-Path $UserRoot 'launcher-profiles.json'
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try {
            $state = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
            if ($null -eq $state -or $state.PSObject.Properties.Name -notcontains 'Profiles' -or
                $state.SelectedProfile -isnot [string] -or $state.Profiles -isnot [array]) { throw 'Invalid save profile list' }
            $seen = @{}
            foreach ($saveProfile in $state.Profiles) {
                $id = [guid]::Empty
                if (-not [guid]::TryParse($saveProfile.Id, [ref]$id) -or $saveProfile.Name.Trim().Length -eq 0 -or $seen.ContainsKey($saveProfile.Id)) {
                    throw 'Invalid save profile'
                }
                $seen[$saveProfile.Id] = $true
            }
        } catch { throw "Cannot read save profiles: $path`nYour saved games have not been changed." }
    }
    return $state
}

function Save-VrLauncherState {
    param([string]$UserRoot, $State)
    [void][IO.Directory]::CreateDirectory($UserRoot)
    $path = Join-Path $UserRoot 'launcher-profiles.json'
    $temporary = $path + '.' + [guid]::NewGuid() + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary, (ConvertTo-Json -InputObject $State -Depth 4), (New-Object Text.UTF8Encoding($false)))
        if ([IO.File]::Exists($path)) { [IO.File]::Replace($temporary, $path, $path + '.bak') }
        else { [IO.File]::Move($temporary, $path) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Get-VrSaveProfiles {
    param([string]$UserRoot)
    $state = Read-VrLauncherState $UserRoot
    [pscustomobject]@{ Id = 'default'; Name = 'Main save'; UserRoot = $UserRoot }
    foreach ($saveProfile in $state.Profiles) {
        [pscustomobject]@{ Id = $saveProfile.Id; Name = $saveProfile.Name; UserRoot = (Join-Path $UserRoot ('save-profiles\' + $saveProfile.Id)) }
    }
}

function New-VrSaveProfile {
    param([string]$UserRoot, [string]$Name)
    $Name = $Name.Trim()
    if ($Name.Length -eq 0 -or $Name.Length -gt 32) { throw 'Enter a profile name between 1 and 32 characters.' }
    if (@(Get-VrSaveProfiles $UserRoot | Where-Object { $_.Name -eq $Name }).Count) { throw 'A save profile with that name already exists.' }
    $state = Read-VrLauncherState $UserRoot
    $saveProfile = [pscustomobject]@{ Id = [guid]::NewGuid().ToString(); Name = $Name }
    $destination = Join-Path $UserRoot ('save-profiles\' + $saveProfile.Id)
    [void][IO.Directory]::CreateDirectory($destination)
    $settings = Join-Path $UserRoot 'settings.toml'
    if (Test-Path -LiteralPath $settings -PathType Leaf) {
        Copy-Item -LiteralPath $settings -Destination (Join-Path $destination 'settings.toml') -ErrorAction Stop
    }
    $state.Profiles = @($state.Profiles) + @($saveProfile)
    $state.SelectedProfile = $saveProfile.Id
    Save-VrLauncherState $UserRoot $state
    return $saveProfile.Id
}

function Get-VrLaunchOptions {
    param([string]$Root = $PSScriptRoot, [ValidateRange(0, 2)][int]$Mode = 0, [string]$ProfileId = 'default', [switch]$Setup)
    $installation = Get-VrInstallation $Root
    if (-not $Setup -and -not (Test-VrGameReady $installation.GameRoot)) {
        throw 'Game files or Title Update 3 are missing or incomplete. Choose Install / finish setup first.'
    }
    $saveProfile = @(Get-VrSaveProfiles $installation.UserRoot | Where-Object { $_.Id -eq $ProfileId })
    if ($saveProfile.Count -ne 1) { throw 'The selected save profile no longer exists.' }
    $arguments = @('--gpu_backend=vulkan', ('--game_data_root="{0}"' -f $installation.GameRoot),
        ('--user_data_root="{0}"' -f $saveProfile[0].UserRoot),
        ('--log_file="{0}\skate3.log"' -f $saveProfile[0].UserRoot))
    if ($Setup) { $arguments += @('--skate3_setup_only=true','--skate3_vr=false','--fullscreen=false') }
    else { $arguments += @('--skate3_vr=true',"--skate3_vr_mode=$Mode",'--skate3_native_render_scene_msaa=1','--skate3_vr_share_frame_work=true') }
    return @{ FilePath = $installation.Executable; WorkingDirectory = $installation.Root; ArgumentList = $arguments }
}

function Update-VrLauncherStatus {
    param($Form)
    $installation = Get-VrInstallation $Form.Tag
    $ready = Test-VrGameReady $installation.GameRoot
    $Form.Controls[0].Controls['GameStatus'].Text = $(if ($ready) { 'Game installed. Choose a save profile and a mode.' } else { 'Game setup is required. Choose Install / finish setup.' })
    foreach ($button in $Form.Controls[0].Controls) {
        if ($button.Name -like 'Mode*') { $button.Enabled = $ready }
    }
    $profiles = @((Get-VrSaveProfiles $installation.UserRoot))
    $selection = $Form.Controls[0].Controls['SaveProfile']
    $selection.Items.Clear()
    $selection.Tag = $profiles
    foreach ($saveProfile in $profiles) { [void]$selection.Items.Add($saveProfile.Name) }
    $selection.SelectedIndex = 0
    $selected = (Read-VrLauncherState $installation.UserRoot).SelectedProfile
    for ($i = 0; $i -lt $profiles.Count; $i++) { if ($profiles[$i].Id -eq $selected) { $selection.SelectedIndex = $i } }
}

function New-VrLauncherForm {
    param([string]$Root = $PSScriptRoot)
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object Windows.Forms.Form
    $form.Text = 'Skate 3 VR'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)
    $form.AutoSize = $true
    $form.AutoSizeMode = 'GrowAndShrink'
    $form.Tag = $Root
    $layout = New-Object Windows.Forms.FlowLayoutPanel
    $layout.FlowDirection = 'TopDown'
    $layout.WrapContents = $false
    $layout.AutoSize = $true
    $layout.AutoSizeMode = 'GrowAndShrink'
    $layout.Padding = New-Object Windows.Forms.Padding(16)
    $form.Controls.Add($layout)

    foreach ($entry in @(@('Instructions',"Connect your headset and start PCVR (Quest Link on Meta).`nConnect your gamepad before choosing a VR mode."), @('GameStatus',''))) {
        $label = New-Object Windows.Forms.Label
        $label.Name = $entry[0]; $label.Text = $entry[1]
        $label.AutoSize = $true
        $label.MaximumSize = New-Object Drawing.Size(430, 0)
        $label.Margin = New-Object Windows.Forms.Padding(3, 3, 3, 12)
        $layout.Controls.Add($label)
    }
    $profileLabel = New-Object Windows.Forms.Label
    $profileLabel.Text = 'Save profile (each profile has its own progress):'
    $profileLabel.AutoSize = $true
    $layout.Controls.Add($profileLabel)
    $selection = New-Object Windows.Forms.ComboBox
    $selection.Name = 'SaveProfile'
    $selection.DropDownStyle = 'DropDownList'
    $selection.Width = 430
    $layout.Controls.Add($selection)

    $newSave = New-Object Windows.Forms.Button
    $newSave.Name = 'NewSave'
    $newSave.Text = 'New save profile...'; $newSave.Width = 430; $newSave.Height = 32
    $newSave.Add_Click({
        param($sender, $eventArgs)
        $window = $sender.FindForm()
        Add-Type -AssemblyName Microsoft.VisualBasic
        $name = [Microsoft.VisualBasic.Interaction]::InputBox('Name for the new save profile:', 'New save profile', '')
        if ($name.Length -eq 0) { return }
        try {
            $installation = Get-VrInstallation $window.Tag
            New-VrSaveProfile -UserRoot $installation.UserRoot -Name $name | Out-Null
            Update-VrLauncherStatus $window
        } catch { [Windows.Forms.MessageBox]::Show($window, $_.Exception.Message, 'Save profiles', 'OK', 'Error') | Out-Null }
    })
    $layout.Controls.Add($newSave)

    $modes = @('Immersive VR', '3D window', '3D window + passthrough')
    for ($mode = 0; $mode -lt $modes.Count; $mode++) {
        $button = New-Object Windows.Forms.Button
        $button.Name = "Mode$mode"; $button.Text = $modes[$mode]
        $button.Size = New-Object Drawing.Size(430, 44)
        $button.Tag = $mode
        $button.Add_Click({
            param($sender, $eventArgs)
            $window = $sender.FindForm()
            try {
                $selector = $window.Controls[0].Controls['SaveProfile']
                $saveProfile = $selector.Tag[$selector.SelectedIndex]
                $options = Get-VrLaunchOptions -Root $window.Tag -Mode ([int]$sender.Tag) -ProfileId $saveProfile.Id
                $installation = Get-VrInstallation $window.Tag
                $state = Read-VrLauncherState $installation.UserRoot
                $state.SelectedProfile = $saveProfile.Id
                Save-VrLauncherState $installation.UserRoot $state
                Start-Process @options -ErrorAction Stop | Out-Null
                $window.Close()
            } catch { [Windows.Forms.MessageBox]::Show($window, $_.Exception.Message, 'Unable to start Skate 3 VR', 'OK', 'Error') | Out-Null }
        })
        $layout.Controls.Add($button)
    }
    $install = New-Object Windows.Forms.Button
    $install.Name = 'InstallGame'; $install.Text = 'Install / finish setup'; $install.Width = 430; $install.Height = 36
    $install.Add_Click({
        param($sender, $eventArgs)
        $window = $sender.FindForm()
        try {
            $options = Get-VrLaunchOptions -Root $window.Tag -Setup
            $process = Start-Process @options -ErrorAction Stop -PassThru
            $sender.Enabled = $false
            foreach ($button in $window.Controls[0].Controls) {
                if ($button.Name -like 'Mode*' -or $button.Name -in @('SaveProfile','NewSave')) { $button.Enabled = $false }
            }
            $timer = New-Object Windows.Forms.Timer
            $timer.Interval = 500
            $timer.Tag = [pscustomobject]@{ Process = $process; Window = $window }
            $timer.Add_Tick({
                param($sender, $eventArgs)
                if ($sender.Tag.Process.HasExited) {
                    $window = $sender.Tag.Window
                    $sender.Stop(); $sender.Tag.Process.Dispose(); $sender.Dispose()
                    if (-not $window.IsDisposed) {
                        $window.Controls[0].Controls['InstallGame'].Enabled = $true
                        $window.Controls[0].Controls['SaveProfile'].Enabled = $true
                        $window.Controls[0].Controls['NewSave'].Enabled = $true
                        Update-VrLauncherStatus $window
                    }
                }
            })
            $timer.Start()
        } catch { $sender.Enabled = $true; [Windows.Forms.MessageBox]::Show($window, $_.Exception.Message, 'Game setup', 'OK', 'Error') | Out-Null }
    })
    $layout.Controls.Add($install)
    $notes = New-Object Windows.Forms.Label
    $notes.Text = "Quality and camera: RB + Start > Video.`nPassthrough must be enabled in Meta Link first."
    $notes.AutoSize = $true
    $notes.MaximumSize = New-Object Drawing.Size(430, 0)
    $notes.Margin = New-Object Windows.Forms.Padding(3, 12, 3, 3)
    $layout.Controls.Add($notes)
    Update-VrLauncherStatus $form
    return $form
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        $launcher = New-VrLauncherForm
        try { [void]$launcher.ShowDialog() } finally { $launcher.Dispose() }
    } catch {
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Skate 3 VR', 'OK', 'Error') | Out-Null
    }
}
