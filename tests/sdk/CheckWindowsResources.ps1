# Checks the resources of an exported Windows game (ADR 0055; run by tests/sdk/ExportGame.cmake): the
# version resource names the product, version and company, and the executable has an icon group.
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Product,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Company
)

$ErrorActionPreference = 'Stop'
$failures = @()
$info = (Get-Item -LiteralPath $Executable).VersionInfo
if ($info.ProductName -ne $Product) { $failures += "ProductName is '$($info.ProductName)', not '$Product'" }
if ($info.ProductVersion -ne $Version) { $failures += "ProductVersion is '$($info.ProductVersion)', not '$Version'" }
if ($info.CompanyName -ne $Company) { $failures += "CompanyName is '$($info.CompanyName)', not '$Company'" }

Add-Type -Namespace IllusionTests -Name Resources -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr LoadLibraryExW(string file, IntPtr reserved, uint flags);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
[DllImport("kernel32.dll")]
public static extern bool FreeLibrary(IntPtr module);
'@
$LOAD_LIBRARY_AS_DATAFILE = 2
$RT_GROUP_ICON = 14
$module = [IllusionTests.Resources]::LoadLibraryExW($Executable, [IntPtr]::Zero, $LOAD_LIBRARY_AS_DATAFILE)
if ($module -eq [IntPtr]::Zero) {
    $failures += "cannot load $Executable as a data file"
} else {
    $icon = [IllusionTests.Resources]::FindResourceW($module, [IntPtr]1, [IntPtr]$RT_GROUP_ICON)
    if ($icon -eq [IntPtr]::Zero) { $failures += "$Executable has no icon group 1" }
    [void][IllusionTests.Resources]::FreeLibrary($module)
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Output $_ }
    exit 1
}
