export function loop(){let sum=0;for(let i=0;i<3;i++){const r=/a/g;sum+=r.lastIndex;r.lastIndex=7;}return sum;}
