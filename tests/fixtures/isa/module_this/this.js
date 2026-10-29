export const direct = () => this;
export const nested = () => { const inner = () => this; return [inner(), typeof this]; };
export const method = { m() { return [this === method, (() => this)() === method]; } };
export const captured = (x) => [this, x];
try { globalThis.__probe(this); } catch (e) { globalThis.__log = [typeof this, e.message]; }
