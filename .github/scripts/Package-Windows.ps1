[CmdletBinding()]
param(
    [ValidateSet('x64', 'arm64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $ProductVersion = '',
    [switch] $Package
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    if ( $ProductVersion -eq '' ) {
        $ProductVersion = $BuildSpec.version
    }

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    if ( $Package ) {
        Log-Group "Building NSIS installer for ${ProductName} (${Target})..."

        # Install NSIS if not already present (CI)
        if ( -not ( Get-Command makensis -ErrorAction SilentlyContinue ) ) {
            Log-Group "Installing NSIS..."
            choco install nsis --no-progress --yes
            $env:PATH += ";C:\Program Files (x86)\NSIS"
        }

        # Run cmake --install to populate the staging directory.
        # The NSIS script sources files from this exact path:
        #   build_${Target}/../release/${Configuration}
        #   = ${ProjectRoot}/release/${Configuration}
        $StageDir = "${ProjectRoot}/release/${Configuration}"
        Invoke-External cmake `
            --install "${ProjectRoot}/build_${Target}" `
            --prefix $StageDir `
            --config $Configuration

        # Build the installer via the package-installer CMake target
        Invoke-External cmake `
            --build "build_${Target}" `
            --target package-installer `
            --config $Configuration

        # Move the generated .exe to the release root alongside the .zip
        $InstallerExe = Get-ChildItem `
            -Path "${ProjectRoot}/build_${Target}" `
            -Filter "${ProductName}-*-windows-*.exe" `
            -Recurse `
            -ErrorAction SilentlyContinue |
            Select-Object -First 1

        if ( $InstallerExe ) {
            Move-Item -Force $InstallerExe.FullName "${ProjectRoot}/release/${OutputName}.exe"
            Write-Output "Installer: ${ProjectRoot}/release/${OutputName}.exe"
        } else {
            Write-Warning "Installer .exe not found after build"
        }

        Log-Group
    }
}

Package
