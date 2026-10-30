// "use concurrent" functions compiled as TypeScript: es2abc emits
// callruntime.notifyconcurrentresult before each return. The body is plain
// JavaScript so Node runs the original as the differential oracle.
export function add(a, b) { "use concurrent"; return a + b; }
export function pick(a) { "use concurrent"; if (a > 1) { return 'big'; } return 'small'; }
export async function later(a) { "use concurrent"; return a * 2; }
