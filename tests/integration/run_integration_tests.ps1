Write-Host "Start script for integration tests" -ForegroundColor Magenta

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$configs = @(
    [pscustomobject]@{
        Name                       = "MinGW, fallback all, no preload"
        FolderName                 = "MinGW_fallback"
        Generator                  = "MinGW Makefiles"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "OFF"
        OpensslShared              = "ON"
        CurlShared                 = "ON"
    },
    [pscustomobject]@{
        Name                       = "MinGW, fallback all, with preload"
        FolderName                 = "MinGW_fallback_preload"
        Generator                  = "MinGW Makefiles"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "ON"
        OpensslShared              = "ON"
        CurlShared                 = "ON"
    },
    [pscustomobject]@{
        Name                       = "MSVC, all shared, fallback all, no preload"
        FolderName                 = "MSVC_shared_fallback"
        Generator                  = "Visual Studio 17 2022"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "OFF"
        OpensslShared              = "ON"
        CurlShared                 = "ON"
    },
    [pscustomobject]@{
        Name                       = "MSVC, all shared, fallback all, with preload"
        FolderName                 = "MSVC_shared_fallback_preload"
        Generator                  = "Visual Studio 17 2022"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "ON"
        OpensslShared              = "ON"
        CurlShared                 = "ON"
    },
    [pscustomobject]@{
        Name                       = "MSVC, shared and static, fallback all, no preload"
        FolderName                 = "MSVC_shared_static_fallback"
        Generator                  = "Visual Studio 17 2022"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "OFF"
        OpensslShared              = "OFF"
        CurlShared                 = "ON"
    },
    [pscustomobject]@{
        Name                       = "MSVC, shared and static, fallback all, with preload"
        FolderName                 = "MSVC_shared_static_fallback_preload"
        Generator                  = "Visual Studio 17 2022"
        UseFallbackOpenSSL         = "ON"
        UseFallbackCurl            = "ON"
        UseFallbackAsio            = "ON"
        UseFallbackSimpleWsServer  = "ON"
        TestPreloadDeps            = "ON"
        OpensslShared              = "OFF"
        CurlShared                 = "ON"
    }
)

$TestExecutables = @(
    "integr_test.exe",
    "backpressure_test.exe",
    "ws_local_test.exe",
    "rate_limit_lifetime_test.exe",
    "request_group_cancel_test.exe",
    "http_client_rate_limit_api_test.exe",
    "sequential_rate_limit_test.exe",
    "rate_limit_partition_test.exe",
    "http_local_test.exe",
    "http_retry_streaming_test.exe",
    "http_response_body_limit_test.exe",
    "http_client_destructor_test.exe"
)

foreach ($cfg in $configs) {
    Write-Host "============ Configuration: $($cfg.Name) ============" -ForegroundColor yellow

    $BuildDir = Join-Path $ScriptDir ("build-" + $cfg.FolderName)
    Write-Host "Build dir: $BuildDir"

    Write-Host "--- Configure ---" -ForegroundColor cyan
    cmake -G "$($cfg.Generator)" `
        -S $ScriptDir `
        -B $BuildDir `
        -DCMAKE_CXX_STANDARD=17 `
        -DCMAKE_CXX_STANDARD_REQUIRED=ON `
        -Wno-dev `
        "-DKURLYK_USE_FALLBACK_OPENSSL=$($cfg.UseFallbackOpenSSL)" `
        "-DKURLYK_USE_FALLBACK_CURL=$($cfg.UseFallbackCurl)" `
        "-DKURLYK_USE_FALLBACK_ASIO=$($cfg.UseFallbackAsio)" `
        "-DKURLYK_USE_FALLBACK_SIMPLE_WS_SERVER=$($cfg.UseFallbackSimpleWsServer)" `
        "-DKURLYK_TEST_PRELOAD_DEPS=$($cfg.TestPreloadDeps)" `
        "-DKURLYK_OPENSSL_SHARED=$($cfg.OpensslShared)" `
        "-DKURLYK_CURL_SHARED=$($cfg.CurlShared)"

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Configure failed for config $($cfg.Name)"
        continue
    }

    Write-Host "--- Build ---" -ForegroundColor cyan
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed for config $($cfg.Name)"
        continue
    }

    $TestDir = $BuildDir
    if ($cfg.Generator -like "Visual Studio*") {
        $TestDir = Join-Path $BuildDir "Release"
    }

    $ConfigFailed = $false
    foreach ($TestExe in $TestExecutables) {
        $ExePath = Join-Path $TestDir $TestExe
        if (-not (Test-Path $ExePath)) {
            Write-Error "Executable not found for config $($cfg.Name): $ExePath"
            $ConfigFailed = $true
            continue
        }

        Write-Host "--- Run $TestExe ($($cfg.Name)) ---" -ForegroundColor cyan
        & $ExePath
        $ExitCode = $LASTEXITCODE
        Write-Host "Exit code: $ExitCode"
        if ($ExitCode -ne 0) {
            Write-Error "$TestExe failed for config $($cfg.Name)"
            $ConfigFailed = $true
        }
    }

    if ($ConfigFailed) {
        continue
    }

    Write-Host "Config $($cfg.Name) finished OK." -ForegroundColor green
}

Write-Host "All configurations finished" -ForegroundColor Magenta
