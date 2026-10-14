export function outer(value) {
    return function factory() {
        return class {
            read() { return value; }
        };
    };
}
