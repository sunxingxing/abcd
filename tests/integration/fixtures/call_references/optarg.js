const o = globalThis.obj;
export const result = o.f(globalThis.G(globalThis.x.g?.h));
export const nested = o.f(o.f(globalThis.x.g?.h ?? 1));
