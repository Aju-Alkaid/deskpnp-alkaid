const fs = require('fs');
let c = fs.readFileSync('E:/Desktop/qiansai/pnp_1/Task/app_host.c', 'utf8');
let lines = c.split('\n');
// Show lines around HCMD_HOLD_TEMP
for (let i = 0; i < lines.length; i++) {
    if (lines[i].includes('HCMD_HOLD_TEMP')) {
        for (let j = i-1; j <= i+6; j++) console.log((j+1) + ': ' + lines[j]);
    }
    if (lines[i].includes('HCMD_LIGHT')) {
        for (let j = i-1; j <= i+10; j++) console.log((j+1) + ': ' + lines[j]);
    }
}
