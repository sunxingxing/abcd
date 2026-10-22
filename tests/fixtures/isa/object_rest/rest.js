export function pick(o) { const {a,b,...rest}=o; return [a,b,rest]; }
export function assign(o) { let a,b,rest; ({a,b,...rest}=o); return [a,b,rest]; }
export function only(o) { const {...rest}=o; return rest; }
export function computed(o) { const {["a"]:a,["b"]:b,...rest}=o; return [a,b,rest]; }
export function copied(o) { const k="k"; const {[k]:value,...rest}=o; return [value,rest]; }
