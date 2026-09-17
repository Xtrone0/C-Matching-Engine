param(
    [ValidateSet('all', 'commands', 'cancel')][string]$Suite = 'all',
    [ValidateRange(1, 100000)][int]$Commands = 10000,
    [ValidateRange(0, 100000)][int]$Initial = 1000,
    [ValidateRange(3, 50)][int]$Runs = 5,
    [ValidateSet(1000, 10000)][int]$LargeCancels = 1000,
    [ValidateSet(1000, 10000, 100000)][int]$MaxOrders = 100000,
    [string]$OutputDirectory,
    [switch]$NoLatency
)
$ErrorActionPreference = 'Stop'
$repoDirectory = Split-Path -Parent $PSScriptRoot
Push-Location $repoDirectory
try {
    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $repoDirectory ('build/benchmarks/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    } elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory = Join-Path $repoDirectory $OutputDirectory
    }
    if ((Test-Path -LiteralPath $OutputDirectory) -and (Get-ChildItem -LiteralPath $OutputDirectory -Force | Select-Object -First 1)) {
        throw 'Choose a new or empty output directory.'
    }
    cmake --build build/release --target order_book_tests order_book_benchmark
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    ctest --test-dir build/release --output-on-failure --output-junit test-results.xml
    if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed; benchmark canceled.' }

    $sourceFiles = @(Get-Item CMakeLists.txt) + @(Get-ChildItem include, src, tests, benchmarks -Recurse -File |
        Where-Object { $_.Extension -in '.hpp', '.cpp', '.in', '.ps1' })
    $sources = @($sourceFiles | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($repoDirectory.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    try {
        $processor = Get-CimInstance Win32_Processor | Select-Object -First 1
        $os = Get-CimInstance Win32_OperatingSystem
        $inventorySource = 'CIM'
    } catch {
        # Some restricted shells cannot query WMI. Registry reads need no service access.
        $cpuRegistry = Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -Name ProcessorNameString
        $osRegistry = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name ProductName, CurrentBuildNumber, UBR
        $processor = [pscustomobject]@{ Name = $cpuRegistry.ProcessorNameString; NumberOfLogicalProcessors = [Environment]::ProcessorCount }
        $os = [pscustomobject]@{ Caption = $osRegistry.ProductName; Version = [Environment]::OSVersion.Version.ToString(); BuildNumber = "$($osRegistry.CurrentBuildNumber).$($osRegistry.UBR)" }
        $inventorySource = 'Registry and runtime; logical processor count is available to the launcher'
    }
    $power = (& powercfg /getactivescheme | Out-String).Trim()
    $arguments = @('--suite', $Suite, '--commands', "$Commands", '--initial', "$Initial", '--runs', "$Runs",
                   '--large-cancels', "$LargeCancels", '--max-orders', "$MaxOrders", '--out', $OutputDirectory)
    if ($NoLatency) { $arguments += '--no-latency' }
    $metadata = [ordered]@{
        started_utc = [DateTime]::UtcNow.ToString('o')
        commit = (& git rev-parse HEAD | Out-String).Trim()
        working_tree = @(& git status --porcelain)
        cpu = $processor.Name
        logical_processors = $processor.NumberOfLogicalProcessors
        windows = $os.Caption
        windows_version = $os.Version
        windows_build = $os.BuildNumber
        inventory_source = $inventorySource
        power_mode = $power
        affinity = 'Inherited from launcher; no explicit core pinning'
        launcher_affinity_mask = ('0x{0:X}' -f (Get-Process -Id $PID).ProcessorAffinity.ToInt64())
        executable_sha256 = (Get-FileHash build/release/order_book_benchmark.exe -Algorithm SHA256).Hash
        arguments = $arguments
        source_files = $sources
    }
    & .\build\release\order_book_benchmark.exe @arguments 2>&1 | Tee-Object -Variable benchmarkConsole
    $benchmarkExit = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath $OutputDirectory)) { New-Item -ItemType Directory -Path $OutputDirectory | Out-Null }
    $benchmarkConsole | Set-Content -LiteralPath (Join-Path $OutputDirectory 'console.txt')
    $metadata['finished_utc'] = [DateTime]::UtcNow.ToString('o')
    $metadata['exit_code'] = $benchmarkExit
    $metadata['workload_files'] = @(Get-ChildItem -LiteralPath (Join-Path $OutputDirectory 'workloads') -File | ForEach-Object {
        [ordered]@{ file = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    $metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'environment.json')
    if ($benchmarkExit -ne 0) { throw "Benchmark failed with exit code $benchmarkExit." }
    Write-Output "Results and machine/build metadata: $OutputDirectory"
} finally {
    Pop-Location
}
