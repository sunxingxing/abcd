// Async functions whose completion catch settles through a register copy of
// the promise (optimized builds copy it before an inner try).
export async function store(o, f) { try { const r = await f(); o.set('k', r); } catch (e) { o.set('e', String(e)); } }
export async function load(o) { if (o.cached) { return o.cached; } try { o.v = await o.load(); } catch (e) { o.v = null; } return o.v; }
export async function nested(o, f) { try { const r = await f(); o.set('k', r); } catch (e) { o.set('e', e.message); } return o.get(); }
