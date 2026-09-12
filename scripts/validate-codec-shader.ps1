param(
    [string]$Source = (Join-Path $PSScriptRoot '..\src\nr_before_sr.cpp'),
    [string[]]$AdditionalShaderSource = @(),
    [string]$PresentHudShaderSource
)

$ErrorActionPreference = 'Stop'
$sourceText = Get-Content -LiteralPath $Source -Raw
$match = [regex]::Match(
    $sourceText,
    'const char kCodecShader\[\] = R"HLSL\((?<shader>[\s\S]*?)\)HLSL";')
if (-not $match.Success) {
    throw 'kCodecShader was not found in the source file.'
}

$shaderBytes = [Text.Encoding]::UTF8.GetBytes($match.Groups['shader'].Value)
if (-not ('NativeD3DCompiler' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NativeD3DCompiler {
    [DllImport("d3dcompiler_47.dll", CharSet = CharSet.Ansi)]
    public static extern int D3DCompile(
        byte[] source, UIntPtr sourceSize, string sourceName,
        IntPtr defines, IntPtr include, string entryPoint, string target,
        uint flags1, uint flags2, out IntPtr code, out IntPtr errors);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate IntPtr GetBufferPointerDelegate(IntPtr self);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate UIntPtr GetBufferSizeDelegate(IntPtr self);

    public static string BlobText(IntPtr blob) {
        if (blob == IntPtr.Zero) return String.Empty;
        IntPtr vtable = Marshal.ReadIntPtr(blob);
        IntPtr getPointerAddress = Marshal.ReadIntPtr(vtable, 3 * IntPtr.Size);
        IntPtr getSizeAddress = Marshal.ReadIntPtr(vtable, 4 * IntPtr.Size);
        var getPointer = (GetBufferPointerDelegate)Marshal.GetDelegateForFunctionPointer(
            getPointerAddress, typeof(GetBufferPointerDelegate));
        var getSize = (GetBufferSizeDelegate)Marshal.GetDelegateForFunctionPointer(
            getSizeAddress, typeof(GetBufferSizeDelegate));
        IntPtr pointer = getPointer(blob);
        ulong length = getSize(blob).ToUInt64();
        if (pointer == IntPtr.Zero || length == 0) return String.Empty;
        int safeLength = checked((int)Math.Min(length, 1024UL * 1024UL));
        return Marshal.PtrToStringAnsi(pointer, safeLength).TrimEnd('\0');
    }

    public static string HResultHex(int value) {
        return unchecked((uint)value).ToString("X8");
    }
}
'@
}

$failed = @()
$entryPoints = @('EncodeCS', 'CombineCS', 'EncodeV2CS', 'CombineV2CS', 'CopyCS', 'ResampleMotionCS')
if ($sourceText.Contains('void ProbePostStoreCS(')) { $entryPoints += 'ProbePostStoreCS' }
if ($sourceText.Contains('void BuildEditFieldCS(')) { $entryPoints += 'BuildEditFieldCS' }
if ($sourceText.Contains('void RestoreEditHorizontalCS(')) { $entryPoints += 'RestoreEditHorizontalCS' }
if ($sourceText.Contains('void RestoreEditReferenceCS(')) { $entryPoints += 'RestoreEditReferenceCS' }
if ($sourceText.Contains('void BuildColorEditFieldCS(')) { $entryPoints += 'BuildColorEditFieldCS' }
if ($sourceText.Contains('void LanczosHorizontalCS(')) { $entryPoints += @('LanczosHorizontalCS','LanczosVerticalCS') }
$shaderJobs = @(@{ Name='NRBeforeSR.Codec'; Bytes=$shaderBytes; Entries=$entryPoints; Target='cs_5_0' })
$presentBaseBody = $null
foreach ($additional in $AdditionalShaderSource) {
    $additionalText = Get-Content -LiteralPath $additional -Raw
    $blocks = [regex]::Matches($additionalText, 'char\s+\w+\[\]\s*=\s*R"HLSL\((?<shader>[\s\S]*?)\)HLSL";')
    if ($blocks.Count -ne 1) { throw "Expected one explicit production shader in $additional" }
    $body = $blocks[0].Groups['shader'].Value
    $entries = @([regex]::Matches($body, '\[numthreads\([^)]*\)\]\s*void\s+(?<entry>\w+)\s*\(') | ForEach-Object { $_.Groups['entry'].Value })
    if ($entries.Count -eq 0 -or @($entries | Select-Object -Unique).Count -ne $entries.Count) { throw "Invalid entry inventory in $additional" }
    $shaderJobs += @{ Name=[IO.Path]::GetFileName($additional); Bytes=[Text.Encoding]::UTF8.GetBytes($body); Entries=$entries; Target='cs_5_0' }
    if ([IO.Path]::GetFileName($additional) -eq 'nr_compat_present_shader.h') {
        $presentBaseBody = $body
        $shaderJobs += @{ Name='Present.Fullscreen'; Bytes=[Text.Encoding]::UTF8.GetBytes($body); Entries=@('FullscreenVS'); Target='vs_5_0' }
        $shaderJobs += @{ Name='Present.Blit'; Bytes=[Text.Encoding]::UTF8.GetBytes($body); Entries=@('BlitPS'); Target='ps_5_0' }
    }
}
if ($PresentHudShaderSource) {
    if (-not $presentBaseBody) { throw 'HUD shader requires an explicit Present base in AdditionalShaderSource.' }
    $hudText = Get-Content -LiteralPath $PresentHudShaderSource -Raw
    $hud = [regex]::Match($hudText, 'char\s+kFgHudShader\[\]\s*=\s*R"HLSL\((?<shader>[\s\S]*?)\)HLSL";')
    $hudRuntime = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PresentHudShaderSource) 'nr_fg_hud_runtime.inl') -Raw
    if (-not $hud.Success -or -not $hudRuntime.Contains('std::string(kCompatPresentShader)+kFgHudShader')) {
        throw 'Production HUD shader composition changed; update the exact compile adapter.'
    }
    $shaderJobs += @{ Name='FG.Hud'; Bytes=[Text.Encoding]::UTF8.GetBytes($presentBaseBody + $hud.Groups['shader'].Value); Entries=@('HudPS'); Target='ps_5_0' }
}
foreach ($job in $shaderJobs) {
    $shaderBytes = $job.Bytes
    $entryPoints = $job.Entries
foreach ($entryPoint in $entryPoints) {
    $code = [IntPtr]::Zero
    $errors = [IntPtr]::Zero
    $result = [NativeD3DCompiler]::D3DCompile(
        $shaderBytes, [UIntPtr]::new($shaderBytes.Length),
        $job.Name, [IntPtr]::Zero, [IntPtr]::Zero,
        $entryPoint, $job.Target, 0x4000, 0, [ref]$code, [ref]$errors)
    try {
        $hex = [NativeD3DCompiler]::HResultHex($result)
        Write-Host ('{0}/{1}: 0x{2}' -f $job.Name, $entryPoint, $hex)
        if ($errors -ne [IntPtr]::Zero) {
            $diagnostic = [NativeD3DCompiler]::BlobText($errors)
            if (-not [string]::IsNullOrWhiteSpace($diagnostic)) {
                Write-Host $diagnostic
            }
        }
        if ($result -lt 0) {
            $failed += ($job.Name + '/' + $entryPoint)
        }
    } finally {
        if ($code -ne [IntPtr]::Zero) {
            [void][Runtime.InteropServices.Marshal]::Release($code)
        }
        if ($errors -ne [IntPtr]::Zero) {
            [void][Runtime.InteropServices.Marshal]::Release($errors)
        }
    }
}
}

if ($failed.Count -ne 0) {
    throw ('Shader entry compilation failed: ' + ($failed -join ', '))
}
