// Conditional and logical arguments of member calls whose arms are bare
// branch edges or removed register moves (optimized builds hoist constants).
export const log = [];
class Target {
  constructor(tag) { this.tag = tag; }
  get m() {
    log.push(this.tag + '.m');
    const self = this;
    return function (...args) {
      log.push(self.tag + ':' + (this === self) + ':' + args.map(x => typeof x === 'object' ? JSON.stringify(x) : x).join(','));
      return args.length;
    };
  }
}
function target(tag) { return new Target(tag); }
export function choose(c) { const o = target('c'); return o.m(c ? 1 : 2); }
export function either(a, b) { const o = target('o'); return o.m(a || b); }
export function both(a) { const o = target('a'); return o.m(1, a && a.x); }
export function fallback(a) { const o = target('n'); return o.m('k', a ?? 'high'); }
export function ordered(x, a) { const o = target('r'); return o.m(x.get(), a ?? 3); }
export function literal(a, v) { const o = target('l'); return o.m(a.tag, {iterations: v ?? 1, delay: a.delay}); }
