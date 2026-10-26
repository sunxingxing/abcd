// Input proof closes; nested pattern/for-of ownership remains a typed refusal.
export async function combined(rows, visit, report) {
  try {
    for (const [value] of rows) await visit(value);
  } catch (error) {
    report("error: " + error?.message + "");
  }
}
