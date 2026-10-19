export function member(receiver, values) {
    return receiver.method(...values);
}
export function direct(fn, values) {
    return fn(...values);
}
export function mixed(receiver, input) {
    return receiver.method(input.first, ...input.values, input.last);
}
export function twice(receiver, left, right) {
    receiver.method(...left);
    return receiver.method(...right);
}
export function handled(receiver, values) {
    try { return receiver.method(...values); }
    catch (error) { return error.constructor.name; }
}
