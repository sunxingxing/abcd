export function run(specifier) { return import(specifier); }
export function observe(input) { const promise = import(input.specifier); input.after(); return promise; }
