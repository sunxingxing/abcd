export function spreadNested(g, h, xs) {
    return g(...h(...xs));
}
