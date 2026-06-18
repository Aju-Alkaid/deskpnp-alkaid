# _replace.ps1 —— 可靠的文本替换工具
# 
# 使用：修改下面三个变量，然后运行 .\tools\_replace.ps1

$TARGET_FILE = "E:\Desktop\qiansai\pnp_1\Task\app_test.c"

$OLD_TEXT = @'
此处粘贴要替换的原始文本
支持多行
'@

$NEW_TEXT = @'
此处粘贴替换后的新文本
支持多行
'@

# ========== 引擎（不用改）==========
$ob = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($OLD_TEXT))
$nb = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($NEW_TEXT))
$f  = $TARGET_FILE -replace '\\', '/'

$js = @"
const fs=require('fs');
let c=fs.readFileSync('$f','utf8');
let o=Buffer.from('$ob','base64').toString('utf8');
let n=Buffer.from('$nb','base64').toString('utf8');
if(c.includes(o)){
    let cnt=c.split(o).length-1;
    c=c.replaceAll(o,n);
    fs.writeFileSync('$f',c,'utf8');
    console.log('OK - replaced '+cnt+' occurrence(s)');
}else{
    console.log('NOT FOUND - no changes made');
}
"@

$js | node -
if ($LASTEXITCODE -eq 0) { Write-Host "SUCCESS" } else { Write-Host "FAILED" }
