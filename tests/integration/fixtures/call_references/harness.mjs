const [file] = process.argv.slice(2);
const log = [];
let version = 0;
const make = tag => function (...v) { log.push(`call ${tag} ${this === globalThis.obj} [${v}]`); return v.length; };
globalThis.obj = {
  get f() { log.push('get f'); return make('f' + version); },
  get nf() { log.push('get nf'); return 5; },
};
globalThis.k = () => { log.push('key'); return 'f'; };
globalThis.H = () => { log.push('H'); return 'h'; };
globalThis.R = v => { log.push('R'); version++; Object.defineProperty(globalThis.obj, 'f', {value: make('replaced')}); return v; };
globalThis.x = { get g() { log.push('get g'); if (globalThis.boom) throw globalThis.sentinel; return { h: 7 }; } };
globalThis.sentinel = { sentinel: true };
globalThis.P = { [Symbol.toPrimitive]() { log.push('prim'); return 'p'; } };
globalThis.Q = { [Symbol.toPrimitive]() { log.push('prim-throw'); throw globalThis.sentinel; } };
globalThis.G = v => { log.push('G ' + v); return v; };
let m, error = null;
try { m = await import(file); } catch (e) { error = e === globalThis.sentinel ? 'sentinel' : e.constructor.name; }
console.log(JSON.stringify({error, exports: m ? Object.fromEntries(Object.keys(m).sort().map(k => [k, m[k]])) : null, log}));
