export function plain(tag,x,y){return tag`a${x}b${y}c`;}
export function member(o,x){return o.tag`a${x}b`;}
export function raw(x){return String.raw`a\n${x}\\\`\${literal}`;}
export function invalid(tag,x){return tag`\xG${x}ok`;}
export function nested(tag,inner,x){return tag`out${inner`in${x}ner`}end`;}
export function effects(o,step){return o.tag`start${step(1)}middle${step(2)}end`;}
export function escapes(tag,x){return tag`tick\` dollar\${ braces \\ newline\n uni\u0061 ${x}end`;}
export function line(tag,x){return tag`first
${x}last`;}
export function thrown(tag,step){return tag`a${step()}b`;}
