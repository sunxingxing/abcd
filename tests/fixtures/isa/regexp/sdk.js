export function basic(){return /a+b/;}
export function empty(){return /(?:)/;}
export function slash(){return /a\/b/;}
export function escaped(){return /a\/b/;}
export function inClass(){return /[\/]/;}
export function unicode(){return /\u0061\u{62}/u;}
export function escapedLines(){return /\n\r\u2028\u2029/;}
export function groups(){return /^(?:a|b)+(?=c)c$/;}
export function globalFlag(){return /a/g;}
export function ignoreFlag(){return /a/i;}
export function multiFlag(){return /^a/m;}
export function dotFlag(){return /a.b/s;}
export function unicodeFlag(){return /a/u;}
export function stickyFlag(){return /a/y;}
export function indicesFlag(){return /a/d;}
export function allFlags(){return /a.b/dgimsuy;}
export function loop(){const values=[];for(let i=0;i<3;i++){const r=/a/g;values.push([r.lastIndex,r.test("a"),r.lastIndex]);}return values;}
export function wide(o){
o.p0;
o.p1;
o.p2;
o.p3;
o.p4;
o.p5;
o.p6;
o.p7;
o.p8;
o.p9;
o.p10;
o.p11;
o.p12;
o.p13;
o.p14;
o.p15;
o.p16;
o.p17;
o.p18;
o.p19;
o.p20;
o.p21;
o.p22;
o.p23;
o.p24;
o.p25;
o.p26;
o.p27;
o.p28;
o.p29;
o.p30;
o.p31;
o.p32;
o.p33;
o.p34;
o.p35;
o.p36;
o.p37;
o.p38;
o.p39;
o.p40;
o.p41;
o.p42;
o.p43;
o.p44;
o.p45;
o.p46;
o.p47;
o.p48;
o.p49;
o.p50;
o.p51;
o.p52;
o.p53;
o.p54;
o.p55;
o.p56;
o.p57;
o.p58;
o.p59;
o.p60;
o.p61;
o.p62;
o.p63;
o.p64;
o.p65;
o.p66;
o.p67;
o.p68;
o.p69;
o.p70;
o.p71;
o.p72;
o.p73;
o.p74;
o.p75;
o.p76;
o.p77;
o.p78;
o.p79;
o.p80;
o.p81;
o.p82;
o.p83;
o.p84;
o.p85;
o.p86;
o.p87;
o.p88;
o.p89;
o.p90;
o.p91;
o.p92;
o.p93;
o.p94;
o.p95;
o.p96;
o.p97;
o.p98;
o.p99;
o.p100;
o.p101;
o.p102;
o.p103;
o.p104;
o.p105;
o.p106;
o.p107;
o.p108;
o.p109;
o.p110;
o.p111;
o.p112;
o.p113;
o.p114;
o.p115;
o.p116;
o.p117;
o.p118;
o.p119;
o.p120;
o.p121;
o.p122;
o.p123;
o.p124;
o.p125;
o.p126;
o.p127;
o.p128;
o.p129;
o.p130;
o.p131;
o.p132;
o.p133;
o.p134;
o.p135;
o.p136;
o.p137;
o.p138;
o.p139;
return /a/g;}
