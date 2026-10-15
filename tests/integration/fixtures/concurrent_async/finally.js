export async function cleanup(value, trace) {
  "use concurrent";
  try { return await value; } finally { trace.push('finally'); }
}
