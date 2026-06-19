# _replace.ps1 -- safe text replace tool (v2)
#
# Usage:
#   1. Edit $TARGET_FILE / $OLD_TEXT / $NEW_TEXT below
#   2. Run:  powershell -ExecutionPolicy Bypass -File .\tools\_replace.ps1
#   3. Dry:  powershell -ExecutionPolicy Bypass -File .\tools\_replace.ps1 -DryRun

param([switch]$DryRun)

$TARGET_FILE = "E:\Desktop\qiansai\pnp_1\Task\app_test.c"

$OLD_TEXT = @'
PASTE_OLD_TEXT_HERE
'@

$NEW_TEXT = @'
PASTE_NEW_TEXT_HERE
'@


# ========== safety guards (v3) ==========
if (-not (Test-Path $TARGET_FILE)) {
    Write-Host "ERROR: target file not found: $TARGET_FILE"
    exit 1
}
if ($OLD_TEXT.Length -eq 0) {
    Write-Host "ERROR: OLD_TEXT is empty — refusing to inject into entire file."
    Write-Host "       Did you forget to edit the OLD_TEXT variable?"
    exit 1
}
$fileContent = [IO.File]::ReadAllText($TARGET_FILE, [Text.Encoding]::UTF8)
if (-not $fileContent.Contains($OLD_TEXT)) {
    Write-Host "WARNING: OLD_TEXT not found in file."
    Write-Host "  File: $TARGET_FILE"
    Write-Host "  First line of search: $($OLD_TEXT.Split("`r`n",2)[0].Substring(0,[Math]::Min(80,$OLD_TEXT.Split("`r`n",2)[0].Length)))"
    Write-Host "  Trying CRLF/LF fallback in node..."
}
# ========== engine (v2, do not edit) ==========
$ob = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($OLD_TEXT))
$nb = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($NEW_TEXT))
$tf = $TARGET_FILE.Replace('\', '/')
$dr = if ($DryRun) { "true" } else { "false" }

# Write node script to temp file (avoids all escaping hell)
$tmpJs = [System.IO.Path]::GetTempFileName() + ".js"
$nodeCode = @'
const fs=require("fs");
const dry=RUN_DRY_VAR;
let c=fs.readFileSync("TGT_FILE_VAR","utf8");
let o=Buffer.from("OLD_B64_VAR","base64").toString("utf8");
let n=Buffer.from("NEW_B64_VAR","base64").toString("utf8");
function R(t,o,n){if(t.includes(o)){let c=t.split(o).length-1;return{t:t.replaceAll(o,n),c};}return null;}
let r=R(c,o,n);
if(!r){let L=o.replace(/\r\n/g,"\n");let cL=c.replace(/\r\n/g,"\n");let nL=n.replace(/\r\n/g,"\n");r=R(cL,L,nL);if(r)r.text=r.text.replace(/\n/g,"\r\n");}
if(!r){r=R(c,o.replace(/\r\n/g,"\n"),n);}
if(!r){r=R(c,o.replace(/\n/g,"\r\n"),n);}
if(r){if(dry){console.log("[DRY] Would replace "+r.c+"x");}else{fs.writeFileSync("TGT_FILE_VAR",r.t,"utf8");console.log("OK - replaced "+r.c+"x");}}
else{console.log("NOT FOUND");let p=o.length>80?o.substring(0,80)+"...":o;console.log("Search: "+JSON.stringify(p));let fl=o.split(/\r?\n/)[0];if(fl.length>8){let i=c.indexOf(fl);console.log(i>=0?"First line at "+i:"First line absent");}}
'@

$nodeCode = $nodeCode.Replace("RUN_DRY_VAR", $dr)
$nodeCode = $nodeCode.Replace("TGT_FILE_VAR", $tf)
$nodeCode = $nodeCode.Replace("OLD_B64_VAR", $ob)
$nodeCode = $nodeCode.Replace("NEW_B64_VAR", $nb)

[System.IO.File]::WriteAllText($tmpJs, $nodeCode, [Text.Encoding]::UTF8)
node $tmpJs
Remove-Item $tmpJs
if ($LASTEXITCODE -eq 0) { Write-Host "SUCCESS" } else { Write-Host "FAILED" }