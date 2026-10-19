#!/usr/bin/env python3
"""Real compiler evidence and exact resource boundaries, never production inputs."""
import pathlib
import subprocess
import sys

probe, compiler, directory = sys.argv[1:]
root = pathlib.Path(directory)
root.mkdir(parents=True, exist_ok=True)
for count in (200, 201, 400, 800):
    source = root / f"self_{count}.js"
    abc = source.with_suffix(".abc")
    source.write_text("export class C {\n" + "\n".join(
        f"static x{i} = new C();" for i in range(count)) + "\n}\n")
    subprocess.run([compiler, "--module", "--merge-abc", "--opt-level=0",
                    f"--record-name=self_{count}", "--output", str(abc), str(source)], check=True)
    result = subprocess.run([probe, str(abc)], check=True, capture_output=True, text=True)
    print(count, result.stdout, end="", flush=True)
