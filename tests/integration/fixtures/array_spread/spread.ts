// Independent source-language oracle: intentionally uses standard iterator spread.
export function only(x) { return [...x]; }
export function mixed(x, y) { return [1, ...x, 2, ...y, 3]; }
export function caught(x) { let result; try { result = [...x]; } catch (e) { result = e.message; } return result; }
