[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$DllPath,

    [Parameter(Mandatory)]
    [string]$DllDirectory
)

$Kernel32Definition = @'
using System;
using System.Runtime.InteropServices;

public class Win32 {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr AddDllDirectory(string newDirectory);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool RemoveDllDirectory(IntPtr cookie);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr hModule);
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
public delegate UIntPtr SearchDelegate(string url, string ticket);
'@

Add-Type -TypeDefinition $Kernel32Definition


function Invoke-Search {
    param (
        [Parameter(Mandatory = $true)]
        [string]$DllPath,

        [Parameter(Mandatory = $true)]
        [string]$DllDirectory,

        [string]$Url,
        [string]$Ticket
    )

    Write-Verbose "Attempting to load DLL from: $DllPath"

    # LOAD_LIBRARY_SEARCH_DEFAULT_DIRS (0x1000) makes LoadLibraryEx search
    # directories registered via AddDllDirectory in addition to the defaults.
    $Cookie = [Win32]::AddDllDirectory((Resolve-Path $DllDirectory).Path)
    if ($Cookie -eq [IntPtr]::Zero) {
        throw "Failed to register DLL directory: $DllDirectory"
    }

    try {
        $DllPointer = [Win32]::LoadLibraryEx((Resolve-Path $DllPath).Path, [IntPtr]::Zero, 0x1000)
        if ($DllPointer -eq [IntPtr]::Zero) {
            throw "Failed to load DLL. Check your path or architecture (x64 vs ARM64)."
        }

        try {
            $FuncPointer = [Win32]::GetProcAddress($DllPointer, "search")
            if ($FuncPointer -eq [IntPtr]::Zero) {
                throw "Function 'search' not found in the DLL."
            }

            $Search = [System.Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
                $FuncPointer,
                [SearchDelegate]
            )

            return $Search.Invoke($Url, $Ticket).ToUInt64()
        }
        finally {
            [void][Win32]::FreeLibrary($DllPointer)
            Write-Verbose "DLL unloaded successfully."
        }
    }
    finally {
        [void][Win32]::RemoveDllDirectory($Cookie)
    }
}

function New-Ticket {
    [CmdletBinding(SupportsShouldProcess)]
    param (
        [Parameter(Mandatory = $true)]
        [Hashtable]$Payload,

        [Parameter(Mandatory = $true)]
        [string]$ApiKey,

        [Parameter(Mandatory = $true)]
        [string]$KeyId
    )

    process {
        if (-not $PSCmdlet.ShouldProcess("ticket")) { return }

        # Helper function to Base64Url encode strings/bytes safely
        function ConvertTo-Base64Url ([byte[]]$bytes) {
            return [Convert]::ToBase64String($bytes).Split('=')[0].Replace('+', '-').Replace('/', '_')
        }

        $Header = @{
            alg = "HS256"
            kid = $KeyId
            typ = "JWT"
        }

        $HeaderJson = $Header  | ConvertTo-Json -Compress
        $PayloadJson = $Payload | ConvertTo-Json -Compress

        $HeaderBytes = [System.Text.Encoding]::UTF8.GetBytes($HeaderJson)
        $PayloadBytes = [System.Text.Encoding]::UTF8.GetBytes($PayloadJson)

        $EncodedHeader = ConvertTo-Base64Url $HeaderBytes
        $EncodedPayload = ConvertTo-Base64Url $PayloadBytes

        $MessageStringToSign = "$EncodedHeader.$EncodedPayload"
        $MessageBytesToSign = [System.Text.Encoding]::UTF8.GetBytes($MessageStringToSign)

        $SecretBytes = [Convert]::FromBase64String($ApiKey)
        $HmacEngine = [System.Security.Cryptography.HMACSHA256]::new($SecretBytes)

        try {
            $SignatureBytes = $HmacEngine.ComputeHash($MessageBytesToSign)
            $EncodedSignature = ConvertTo-Base64Url $SignatureBytes

            return "$MessageStringToSign.$EncodedSignature"
        }
        finally {
            $HmacEngine.Dispose()
        }
    }
}

$AccountsTicket = New-Ticket -Payload @{
    data = @{
        service = 'Accounts'
        id      = [System.Guid]::NewGuid().ToString()
        data    = $null
    }
    exp  = [DateTimeOffset]::UtcNow.AddMinutes(10).ToUnixTimeSeconds()
} -ApiKey $env:YAXI_API_KEY -KeyId $env:YAXI_KEY_ID

$Result = Invoke-Search -DllPath $DllPath -DllDirectory $DllDirectory -Url $env:ROUTEX_URL -Ticket $AccountsTicket
if ($Result -ne 20) {
    throw "Expected 20 results, but got $Result."
}
