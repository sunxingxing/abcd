export function run(source) { try { return {0: source.first(), 7: source.second()}; } catch (error) { return error.name; } }
