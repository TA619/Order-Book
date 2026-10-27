# Downloads full-day LOBSTER level-10 sample files (NASDAQ, 2012-06-21) into data/.
#
# lobsterdata.com now needs an account to download its free samples, so this
# pulls the same public sample files from GitHub mirrors (Git LFS).
#
#   powershell -ExecutionPolicy Bypass -File scripts\download_data.ps1

$ErrorActionPreference = "Stop"
$data = Join-Path $PSScriptRoot "..\data"

$sources = @{
    "AMZN" = "https://media.githubusercontent.com/media/kpetridis24/lobsim/master/sample_data"
    "MSFT" = "https://media.githubusercontent.com/media/Clydexy/Distributed-DeepLOB/main/datasets"
}

foreach ($ticker in $sources.Keys) {
    foreach ($kind in "message", "orderbook") {
        $name = "${ticker}_2012-06-21_34200000_57600000_${kind}_10.csv"
        $dest = Join-Path $data $name
        if (Test-Path $dest) {
            Write-Host "have $name"
            continue
        }
        Write-Host "downloading $name"
        Invoke-WebRequest -Uri "$($sources[$ticker])/$name" -OutFile $dest
    }
}
