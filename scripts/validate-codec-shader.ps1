param(
    [string]$Source = (Join-Path $PSScriptRoot '..\src\nr_before_sr.cpp')
)

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
}
'@
}

foreach ($entryPoint in @('EncodeCS', 'CombineCS', 'CopyCS', 'ResampleMotionCS')) {
    $code = [IntPtr]::Zero
    $errors = [IntPtr]::Zero
    $result = [NativeD3DCompiler]::D3DCompile(
        $shaderBytes, [UIntPtr]::new($shaderBytes.Length),
        'NRBeforeSR.Codec', [IntPtr]::Zero, [IntPtr]::Zero,
        $entryPoint, 'cs_5_1', 0x4000, 0, [ref]$code, [ref]$errors)
    if ($code -ne [IntPtr]::Zero) {
        [void][Runtime.InteropServices.Marshal]::Release($code)
    }
    if ($errors -ne [IntPtr]::Zero) {
        [void][Runtime.InteropServices.Marshal]::Release($errors)
    }
    Write-Host ('{0}: 0x{1:X8}' -f $entryPoint, [uint32]$result)
    if ($result -lt 0) {
        throw "$entryPoint failed to compile."
    }
}
