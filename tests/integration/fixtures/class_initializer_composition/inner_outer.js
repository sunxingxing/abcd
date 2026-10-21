let outer = 'first';
export class C { static x = C; static y = outer; static read() { return C; } }
outer = 'last';
