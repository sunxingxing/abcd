export function save(a) { this.params = arguments; return this.params; }
export function apply(f) { return f.apply(this, arguments); }
export function inspect(a) { return [arguments.length, arguments[0], arguments === arguments]; }
export function captured(a) { const saved = arguments; return function () { return saved; }; }
export function changed(a) { const saved = arguments; a = 42; return [saved, arguments]; }
export class Holder { constructor(a) { this.params = arguments; } }
