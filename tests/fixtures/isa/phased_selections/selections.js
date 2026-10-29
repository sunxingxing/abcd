// Conditional and logical values on the phased route (async functions and
// functions with try): bare arms, string arms kept behind register copies,
// member-call arguments and plain consumers.
export const log = [];
class Target {
  constructor(tag) { this.tag = tag; }
  get set() {
    log.push(this.tag + '.set');
    const self = this;
    return function (...args) { log.push(self.tag + ':' + (this === self) + ':' + args.join(',')); return args.length; };
  }
}
export async function pick(t) { const o = new Target('p'); o.set('k', t.p ? 'a' : 'b'); return log.length; }
export async function fallback(t) { const o = new Target('f'); return o.set('k', t ?? 'x'); }
export async function either(f, x) { const o = new Target('e'); return o.set('n', f() || x); }
export function guarded(f, t, u) { try { f(); } catch (e) { log.push('caught'); } return t ? 1 : u; }
export async function plain(t) { return t ?? 'x'; }
export async function joined(t, u) { return t || u; }
export async function passed(f, t) { const v = t ?? 'x'; await f(v); return v; }
