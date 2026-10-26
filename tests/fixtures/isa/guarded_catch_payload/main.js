export function sync(rows, visit, report) {
  for (const value of rows) {
    try {
      visit(value);
    } catch (error) {
      report("error: " + error?.message + "");
    }
  }
}
export function pattern(rows, visit, report) {
  try {
    let index = 0;
    while (index < rows.length) {
      const [value] = rows[index];
      visit(value);
      index++;
    }
  } catch (error) {
    report("error: " + error?.message + "");
  }
}
export async function asyncBody(rows, visit, report) {
  for (const value of rows) {
    try {
      await visit(value);
    } catch (error) {
      report("error: " + error?.message + "");
    }
  }
}
export async function asyncPattern(rows, visit, report) {
  try {
    const [value] = rows;
    await visit(value);
  } catch (error) {
    report("error: " + error?.message + "");
  }
}
