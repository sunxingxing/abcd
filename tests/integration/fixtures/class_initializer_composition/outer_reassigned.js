export class C { static x = C; static read() { return C; } }
export const originalClass = C;
C = 17;
