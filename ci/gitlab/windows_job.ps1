# GitLab Windows job (GitLab's hosted Windows runner, PowerShell; or a self-hosted runner): installs what a
# GitHub windows-latest runner has before the first step and the GitHub job needs (python.org Python 3.14,
# MSYS2 with -Msys2, Git Bash), then runs the GitHub job's steps with ci/gitlab/gh_steps.py.
# Everything downloaded is pinned by SHA-256. A self-hosted runner that already has them skips the downloads.
#
#   powershell -File ci/gitlab/windows_job.ps1 -Workflow .github/workflows/X.yml -Job JOB [-Matrix k=v,k=v] [-Msys2]
param([Parameter(Mandatory = $true)][string]$Workflow, [Parameter(Mandatory = $true)][string]$Job,
      [string]$Matrix = '', [switch]$Msys2)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Rest = @($Workflow, $Job)
foreach ($m in ($Matrix -split ',' | Where-Object { $_ })) { $Rest += @('--matrix', $m) }
$arm = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64'
$tools = Join-Path $env:CI_PROJECT_DIR '..\wwhd-ci-tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null

function Get-Pinned($url, $sha256, $out) {
  if (-not (Test-Path $out)) { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out }
  $have = (Get-FileHash -Algorithm SHA256 $out).Hash.ToLower()
  if ($have -ne $sha256) { Remove-Item $out; throw "checksum mismatch for $url ($have)" }
}

# python.org Python 3.14.8 (the release ships the same version's embeddable build: tools/installer/toolchains.json)
$py = Join-Path $tools 'python314\python.exe'
if (-not (Test-Path $py)) {
  if ($arm) { $u = 'https://www.python.org/ftp/python/3.14.8/python-3.14.8-arm64.exe'; $s = '53ba74b5b4370eb823fe0005eedeab907479f7339cecb11efea0e75ab5f10cfc' }
  else { $u = 'https://www.python.org/ftp/python/3.14.8/python-3.14.8-amd64.exe'; $s = '759be887b96e736a3ca886daf8d575f18fcae1a09efab6902f42d59e8999f8ef' }
  $inst = Join-Path $tools 'python-installer.exe'
  Get-Pinned $u $s $inst
  $p = Start-Process -FilePath $inst -Wait -PassThru -ArgumentList '/quiet', 'InstallAllUsers=0', 'PrependPath=0',
    'Include_test=0', 'Include_launcher=0', "TargetDir=$(Join-Path $tools 'python314')"
  if ($p.ExitCode -ne 0) { throw "Python installer failed ($($p.ExitCode))" }
  Remove-Item $inst
}
& $py -m pip install --quiet --disable-pip-version-check pyyaml==6.0.3
if ($LASTEXITCODE -ne 0) { throw 'pip install pyyaml failed' }
$env:WWHD_SETUP_PYTHON = $py   # actions/setup-python steps use this Python
$env:PATH = "$(Split-Path $py);$(Join-Path (Split-Path $py) 'Scripts');$env:PATH"
# GitHub's Windows image has CMake; GitLab's may not (the release's Git Bash steps call cmake)
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  & $py -m pip install --quiet --disable-pip-version-check cmake==3.31.6
  if ($LASTEXITCODE -ne 0) { throw 'pip install cmake failed' }
}

# MSYS2 (msys2/setup-msys2 steps install their packages into it)
if ($Msys2) {
  if (-not $env:WWHD_MSYS2_ROOT) { $env:WWHD_MSYS2_ROOT = 'C:\msys64' }
  if (-not (Test-Path (Join-Path $env:WWHD_MSYS2_ROOT 'usr\bin\bash.exe'))) {
    if ($arm) { throw 'no MSYS2 on this Windows arm64 runner: install it at C:\msys64 (or set WWHD_MSYS2_ROOT)' }
    $sfx = Join-Path $tools 'msys2-base.sfx.exe'
    Get-Pinned 'https://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20260927.sfx.exe' 'ad336cccfda47758b5e15cda993fbba421115cb0b126697daef1ee4dfe37209f' $sfx
    $parent = Split-Path $env:WWHD_MSYS2_ROOT
    & $sfx -y "-o$parent\" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'MSYS2 unpack failed' }
    Remove-Item $sfx
    $bash = Join-Path $env:WWHD_MSYS2_ROOT 'usr\bin\bash.exe'
    & $bash -lc 'true'   # first start: keyring and home folder
    if ($LASTEXITCODE -ne 0) { throw 'MSYS2 first start failed' }
  }
}

# Git Bash: GitHub's "shell: bash" on Windows
if (-not $env:WWHD_BASH) {
  $gitbash = Join-Path $env:ProgramFiles 'Git\bin\bash.exe'
  if (-not (Test-Path $gitbash)) { $gitbash = (Join-Path (Split-Path (Split-Path (Get-Command git).Source)) 'bin\bash.exe') }
  if (-not (Test-Path $gitbash)) { throw 'no Git Bash on this runner (install Git for Windows or set WWHD_BASH)' }
  $env:WWHD_BASH = $gitbash
}

Set-Location $env:CI_PROJECT_DIR
& $py ci/gitlab/gh_steps.py @Rest
exit $LASTEXITCODE
