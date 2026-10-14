"""Regenerate ABC-only preparation fixtures; never imported by production."""
from pathlib import Path
import subprocess
compiler=Path('/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc')
root=Path(__file__).resolve().parent
for shape in ('width','depth'):
    for count in (200,201,400,800):
        name=f'{shape}{count}'
        if shape=='width':
            source='export function run() {\n'+''.join(f'class C{i} {{}}\n' for i in range(count))+f'return C{count-1};\n}}\n'
        else:
            source='export '+''.join(f'function f{i}() {{\n' for i in range(count))+'class End {}\nreturn End;\n}\n'+''.join(f'return f{i+1};\n}}\n' for i in reversed(range(count-1)))
        path=root/(name+'.js');path.write_text(source)
        subprocess.run([str(compiler),'--module','--merge-abc','--opt-level=0',f'--record-name={name}',f'--source-file={name}.js','--output',str(root/(name+'.abc')),str(path)],check=True)

# A class-member family root is also a normal descendant creator.
subprocess.run([str(compiler),'--module','--merge-abc','--opt-level=0','--record-name=member','--source-file=member.js','--output',str(root/'member.abc'),str(root/'member.js')],check=True)
